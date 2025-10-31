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
	using FrameCallback = std::function<void(const VideoFrame&)>;
	void set_frame_callback(FrameCallback callback) { frame_callback_=callback; }
	
	int  get_capture_width() const{ return texture_width_; }
	int  get_capture_height() const{ return texture_height_; }

	private:

	bool init();
	void capture_thread();
	bool convert_texture_to_yuv(ID3D11Texture2D* texture,VideoFrame& frame);
	bool handle_device_lost();
	bool create_staging_texture(UINT width,UINT height);

	CaptureConfig config_;
	FrameCallback frame_callback_;

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

