#pragma once
#include<d3d11.h>
#include<dxgi1_2.h>
#include<dxgi1_3.h>
#include<wrl.h>
#include<vector>
#include<mutex>
#include<functional>
#include<thread>
#include<atomic>
#include "time_manager.h"

extern"C"{
	#include<libswscale/swscale.h>
	#include<libavutil/pixfmt.h>
}

using Microsoft::WRL::ComPtr;

struct CaptureConfig{
	bool capture_full_screen = true;	//全屏
	HWND target_window = nullptr;		//目标窗口
	int frame_rate = 60;				//帧率
	bool use_hardware_conversion = true;//硬件格式转换

	int target_width = 0;      // 0 表示使用原始分辨率
    int target_height = 0;     // 0 表示使用原始分辨率
    bool maintain_aspect_ratio = true;  // 是否保持宽高比

	bool capture_region= false;
	RECT capture_rect={0,0,0,0};
	int region_width = 0;
	int region_height = 0;

	bool enable_region_optimization = true; 
	bool use_crop_only=false;
	int region_quality=1;
	bool dynamic_region_adjustment=true;
	int capture_padding=0;

};

struct VideoFrame {
	std::vector<uint8_t> data;
	int width = 0;
	int height = 0;
	size_t size = 0;
	int64_t timestamp = 0;
};

class DXGICapture
{

	public:
	DXGICapture(const CaptureConfig& config);
	~DXGICapture();

	bool start();
	void stop();
	bool get_latest_frame(VideoFrame& frame);
	bool set_capture_region(int x,int y,int width,int height);
	bool set_capture_window(HWND window);
	bool update_region_dynamically();

	using FrameCallback = std::function<void(const VideoFrame&)>;
	void set_frame_callback(FrameCallback callback) { frame_callback_=callback; }
	
	int  get_capture_width() const{ return texture_width_; }

	private:

	bool init();
	void capture_thread();
	bool convert_texture_to_yuv(ID3D11Texture2D* texture,VideoFrame& frame);
	bool handle_device_lost();
	bool create_staging_texture(UINT width,UINT height);

	void calculate_target_resolution(int src_width, int src_height, int& target_width, int& target_height);
    bool validate_region_parameters(D3D11_TEXTURE2D_DESC& desc);
    bool process_region_capture(D3D11_MAPPED_SUBRESOURCE& mapped_resource, VideoFrame& frame, D3D11_TEXTURE2D_DESC& desc);
    bool process_region_fast(D3D11_MAPPED_SUBRESOURCE& mapped_resource, VideoFrame& frame, D3D11_TEXTURE2D_DESC& desc);
    bool process_region_balanced(D3D11_MAPPED_SUBRESOURCE& mapped_resource, VideoFrame& frame, D3D11_TEXTURE2D_DESC& desc);
    bool process_region_high_quality(D3D11_MAPPED_SUBRESOURCE& mapped_resource, VideoFrame& frame, D3D11_TEXTURE2D_DESC& desc);
    bool create_sws_context_for_region(int src_width, int src_height, int dst_width, int  dst_height);
    bool process_fullscreen_capture(D3D11_MAPPED_SUBRESOURCE& mapped_resource, VideoFrame& frame, D3D11_TEXTURE2D_DESC& desc);

	FrameCallback frame_callback_;
	CaptureConfig config_;

	//COM
	ComPtr<IDXGIFactory2> dxgi_factory_;
	ComPtr<ID3D11Device> d3d_device_;
	ComPtr<ID3D11DeviceContext> d3d_context_;
	ComPtr<IDXGIOutputDuplication> duplication_;
	ComPtr<ID3D11Texture2D> staging_texture_;
	
	//swscale
	SwsContext* sws_context_=nullptr;
	int texture_width_ = 0;
	int texture_height_ = 0;

	//捕获状态
	std::atomic<bool> running_{ false };
	std::thread capture_thread_;
	std::mutex frame_mutex_;
	VideoFrame latest_frame_;
	std::vector<uint8_t> frame_buffer_;

	//设备状态
	std::atomic<bool> device_lost_{false};
};

