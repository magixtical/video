#include "dxgi_capture.h"
#include<chrono>
#include<stdexcept>
#include<iostream>

#pragma comment(lib,"swscale.lib")
#pragma comment(lib,"avutil.lib")
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "dxguid.lib")

static ComPtr<ID3D11Device> create_d3d_device() {
	ComPtr<ID3D11Device> device;
	D3D_FEATURE_LEVEL feature_level;
	UINT flags=D3D11_CREATE_DEVICE_BGRA_SUPPORT;
#ifdef _DEBUG
	flags|=D3D11_CREATE_DEVICE_DEBUG;
#endif

	HRESULT hr = D3D11CreateDevice(
		nullptr,
		D3D_DRIVER_TYPE_HARDWARE,
		nullptr,
		flags,
		nullptr,
		0,
		D3D11_SDK_VERSION,
		&device,
		&feature_level,
		nullptr
	);

	if (FAILED(hr)) {
		throw std::runtime_error("Failed to create D3D11 device");
	}
	return device;
}

DXGICapture::DXGICapture(const CaptureConfig& config):config_(config){
	if (!init()) {
		throw std::runtime_error("DXGI capture initialization failed");
	}
}

DXGICapture::~DXGICapture() {
	stop();
	if(sws_context_){
		sws_freeContext(sws_context_);
		sws_context_=nullptr;
	}
}

bool DXGICapture::init() {
	HRESULT hr = CreateDXGIFactory2(0, IID_PPV_ARGS(&dxgi_factory_));
	if (FAILED(hr)) {
		std::cerr << "CreateDXGIFactory2 failed:" << hr << std::endl;
		return false;
	}

	d3d_device_ = create_d3d_device();
	if (!d3d_device_) {
		return false;
	}

	d3d_device_->GetImmediateContext(&d3d_context_);
	
	//初始化屏幕复制
	ComPtr<IDXGIAdapter1> adapter;
    for (UINT i = 0; dxgi_factory_->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND; ++i) {
        ComPtr<IDXGIOutput> output;
        for (UINT j = 0; adapter->EnumOutputs(j, &output) != DXGI_ERROR_NOT_FOUND; ++j) {
            DXGI_OUTPUT_DESC output_desc;
            if (SUCCEEDED(output->GetDesc(&output_desc))) {
                if (config_.capture_full_screen || output_desc.AttachedToDesktop) {
                    ComPtr<IDXGIOutput1> output1;
                    if (SUCCEEDED(output.As(&output1))) {
                        hr = output1->DuplicateOutput(d3d_device_.Get(), &duplication_);
                        if (SUCCEEDED(hr)) {
                            DXGI_OUTDUPL_DESC dupl_desc;
                            duplication_->GetDesc(&dupl_desc);
                            
                            // 创建staging纹理
                            if (create_staging_texture(dupl_desc.ModeDesc.Width, dupl_desc.ModeDesc.Height)) {
                                std::cout << "DXGI capture initialized successfully: " 
                                          << dupl_desc.ModeDesc.Width << "x" << dupl_desc.ModeDesc.Height 
                                          << std::endl;
                                return true;
                            }
                        }
                    }
                }
            }
            output.Reset();
        }
        adapter.Reset();
    }

    std::cerr << "Failed to initialize output duplication" << std::endl;
    return false;
}


bool DXGICapture::create_staging_texture(UINT width, UINT height) {
	if(sws_context_){
		sws_freeContext(sws_context_);
		sws_context_=nullptr;
	}
	texture_height_=height;
	texture_width_=width;

	//staging texture(保持全屏大小)
	D3D11_TEXTURE2D_DESC desc={};
	desc.Width = width;
	desc.Height = height;
	desc.MipLevels = 1;
	desc.ArraySize=1;
	desc.Format=DXGI_FORMAT_B8G8R8A8_UNORM;  //BGRA
	desc.SampleDesc.Count=1;
	desc.SampleDesc.Quality=0;
	desc.Usage=D3D11_USAGE_STAGING;
	desc.BindFlags=0;
	desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ;
	desc.MiscFlags=0;

	HRESULT hr=d3d_device_->CreateTexture2D(&desc,nullptr,&staging_texture_);
	if(FAILED(hr)){
		std::cerr<<"Failed to create staging texture"<<std::endl;
		return false;
	}
	int src_width = config_.capture_region ? config_.region_width : width;
    int src_height = config_.capture_region ? config_.region_height : height;
    int target_width = config_.target_width > 0 ? config_.target_width : src_width;
    int target_height = config_.target_height > 0 ? config_.target_height : src_height;

	if (config_.maintain_aspect_ratio && config_.target_width > 0 && config_.target_height > 0) {
        float src_aspect = (float)src_width / src_height;
        float dst_aspect = (float)config_.target_width / config_.target_height;
        
        if (src_aspect > dst_aspect) {
            target_height = (int)(config_.target_width / src_aspect);
        } else {
            target_width = (int)(config_.target_height * src_aspect);
        }
    }

    sws_context_ = sws_getContext(
		src_width, 
		src_height, 
		AV_PIX_FMT_BGRA, 
        target_width, 
		target_height, 
		AV_PIX_FMT_YUV420P, 
        SWS_BILINEAR, 
		nullptr, 
		nullptr, 
		nullptr
	);
    
    if(!sws_context_){
        std::cerr<<"Failed to create sws_context"<<std::endl;
        return false;
    }

    std::cout << "Scaling configured: " << src_width << "x" << src_height 
              << " -> " << target_width << "x" << target_height << std::endl;
    return true;
}


bool DXGICapture::start() {
	if (running_)
		return true;
	if (!duplication_ || !d3d_device_ || !d3d_context_) {
		if (!init()) {  
			std::cerr << "Failed to reinitialize DXGICapture on start()" << std::endl;
			return false;
		}
	}
	running_ = true;
	capture_thread_ = std::thread(&DXGICapture::capture_thread, this);
	return true;
}

void DXGICapture::stop() {
	if (!running_)
		return;
	running_ = false;
	if (capture_thread_.joinable()) {
		capture_thread_.join();
	}
	duplication_.Reset();
}

void DXGICapture::capture_thread() {
	const auto frame_interval = std::chrono::milliseconds(1000 / config_.frame_rate);
	DXGI_OUTDUPL_FRAME_INFO frame_info;
	ComPtr<IDXGIResource> resource;

	while (running_) {
		const auto start_time = std::chrono::steady_clock::now();

		if (!duplication_) {
			std::cerr << "duplication_ is null, reinitializing..." << std::endl;
			if (!init()) {  
				std::this_thread::sleep_for(frame_interval);
				continue;
			}
		}

		//尝试获取下一帧
		HRESULT hr = duplication_->AcquireNextFrame(100, &frame_info,&resource);
		if (FAILED(hr)) {
			if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET) {
				std::cerr << "Device lost,reinitializing..." << std::endl;
				device_lost_ = true;
				if (!handle_device_lost()) {
					std::this_thread::sleep_for(frame_interval);
				}
			}
			std::this_thread::sleep_for(frame_interval);
			continue;
		}

		ComPtr<ID3D11Texture2D> texture;
		if (resource.As(&texture) != S_OK) {
			duplication_->ReleaseFrame();
			continue;
		}

		//转化为YUV420P并缓存
		VideoFrame frame;
		if (convert_texture_to_yuv(texture.Get(),frame)) {
			frame.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
				std::chrono::system_clock::now().time_since_epoch()
			).count();

			std::lock_guard<std::mutex> lock(frame_mutex_);
			if (frame_callback_) {
				frame_callback_(frame);
			}

			latest_frame_=std::move(frame);
		}

		duplication_->ReleaseFrame();

		const auto elapsed = std::chrono::steady_clock::now() - start_time;
		if (elapsed < frame_interval) {
			std::this_thread::sleep_for(frame_interval - elapsed);
		}
	}
}

bool DXGICapture::convert_texture_to_yuv(ID3D11Texture2D* texture, VideoFrame& frame) {
	if (!texture || !staging_texture_)
		return false;

	D3D11_TEXTURE2D_DESC desc;
	texture->GetDesc(&desc);

	int target_width = config_.target_width > 0 ? config_.target_width : desc.Width;
    int target_height = config_.target_height > 0 ? config_.target_height : desc.Height;
    
	if(config_.capture_region){
		target_width=config_.region_width;
		target_height=config_.region_height;
	}

    // 如果保持宽高比，调整目标分辨率
    if (config_.maintain_aspect_ratio && config_.target_width > 0 && config_.target_height > 0) {
        float src_aspect = (float)desc.Width / desc.Height;
        float dst_aspect = (float)config_.target_width / config_.target_height;
        
        if (src_aspect > dst_aspect) {
            target_height = (int)(config_.target_width / src_aspect);
        } else {
            target_width = (int)(config_.target_height * src_aspect);
        }
    }


	frame.width = target_width;
    frame.height = target_height;
    frame.size = target_width * target_height * 3 / 2;
    frame.data.resize(frame.size);

	d3d_context_->CopyResource(staging_texture_.Get(), texture);

	D3D11_MAPPED_SUBRESOURCE mapped_resource;
	HRESULT hr = d3d_context_->Map(staging_texture_.Get(), 0, D3D11_MAP_READ, 0, &mapped_resource);
	if (FAILED(hr)) {
		return false;
	}

	if (sws_context_) {
        uint8_t* src_data[4] = { static_cast<uint8_t*>(mapped_resource.pData), nullptr, nullptr, nullptr };
        int src_linesize[4] = { static_cast<int>(mapped_resource.RowPitch), 0, 0, 0 };

		if(config_.capture_region){
			int start_x = config_.capture_rect.left;
            int start_y = config_.capture_rect.top;
            src_data[0] += start_y * mapped_resource.RowPitch + start_x * 4;
		}

        uint8_t* y_plane = frame.data.data();
        uint8_t* u_plane = y_plane + target_width * target_height;
        uint8_t* v_plane = u_plane + (target_width / 2) * (target_height / 2);
        uint8_t* dst_data[4] = { y_plane, u_plane, v_plane, nullptr };
        int dst_linesize[4] = {
            target_width,         // Y分量行宽
            target_width / 2,     // U分量行宽
            target_width / 2,     // V分量行宽
            0
        };

        // 执行格式转换和缩放
        int converted_lines = sws_scale(
            sws_context_,
            src_data, 
			src_linesize,
            0, 
			config_.capture_region?config_.region_height:desc.Height,
            dst_data, 
			dst_linesize
        );

        d3d_context_->Unmap(staging_texture_.Get(), 0);

        if (converted_lines != target_height) {
            std::cerr << "sws_scale failed: converted " << converted_lines
                << " lines (expected " << target_height << ")" << std::endl;
            return false;
        }
    }
    else {
        const uint8_t* src_data = static_cast<const uint8_t*>(mapped_resource.pData);
        uint8_t* y_plane = frame.data.data();
        uint8_t* u_plane = y_plane + target_width * target_height;
        uint8_t* v_plane = u_plane + (target_width / 2) * (target_height / 2);

        int start_x = config_.capture_region ? config_.capture_rect.left : 0;
        int start_y = config_.capture_region ? config_.capture_rect.top : 0;
        int end_x = config_.capture_region ? config_.capture_rect.right : desc.Width;
        int end_y = config_.capture_region ? config_.capture_rect.bottom : desc.Height;

        for (int y = start_y; y < end_y; ++y) {
            for (int x = start_x; x < end_x; ++x) {
                const uint8_t* pixel = src_data + (y * mapped_resource.RowPitch + x * 4);
                uint8_t b = pixel[0];
                uint8_t g = pixel[1];
                uint8_t r = pixel[2];

                // 计算目标像素位置
                int dst_y = y - start_y;
                int dst_x = x - start_x;
                
                // 计算Y分量
                y_plane[dst_y * target_width + dst_x] = static_cast<uint8_t>(
                    (0.299 * r + 0.587 * g + 0.114 * b)
                );

                // 下采样UV分量
                if ((dst_y % 2 == 0) && (dst_x % 2 == 0)) {
                    int uv_index = (dst_y / 2) * (target_width / 2) + (dst_x / 2);
                    u_plane[uv_index] = static_cast<uint8_t>(
                        (-0.169 * r - 0.331 * g + 0.5 * b) + 128
                    );
                    v_plane[uv_index] = static_cast<uint8_t>(
                        (0.5 * r - 0.419 * g - 0.081 * b) + 128
                    );
                }
            }
        }

        d3d_context_->Unmap(staging_texture_.Get(), 0);
    }

    return true;
}

bool DXGICapture::handle_device_lost() {
	duplication_.Reset();
	staging_texture_.Reset();
	if(sws_context_){
		sws_freeContext(sws_context_);
		sws_context_=nullptr;
	}

	d3d_context_.Reset();
	d3d_device_.Reset();
	dxgi_factory_.Reset();

	bool success = init();
	device_lost_ = !success;
	return success;
}

bool DXGICapture::get_latest_frame(VideoFrame& frame) {
	std::scoped_lock<std::mutex> lock(frame_mutex_);
	if (latest_frame_.data.empty())
		return false;
	frame = latest_frame_;
	return true;
}