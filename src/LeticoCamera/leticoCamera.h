#pragma once

#include <camera/camera.h>
#include <camera/device_discovery.h>
#include <camera/photography_settings.h>

#include <iostream>
#include <json.hpp>
#include <string>
#include <thread>
#include <vector>
#include <yaml-cpp/yaml.h>

#include "../utils.hpp"

using json = nlohmann::json;
class LeticoCamera
{
public:
	LeticoCamera();

	~LeticoCamera();

	std::string takePhoto();
	std::string getSerialNumber();
	std::vector<std::string> getAllSerialNumbers();
	std::vector<std::string> getFileList();
	void deleteFile(const std::string& file_to_delete);
	std::string downloadFile(std::string file_to_download);
	std::vector<std::string> downloadFile(const std::vector<std::string>& files_to_download);

	void downloadAllFiles();

	std::string startRecording(
		int bitrate = 1024 * 1024 * 10,
		ins_camera::VideoResolution resolution = ins_camera::VideoResolution::RES_3040_3040P24,
		ins_camera::CameraFunctionMode functionMode = ins_camera::CameraFunctionMode::FUNCTION_MODE_NORMAL_VIDEO
	);
	std::vector<std::string> stopRecording();
	std::string startPreviewLiveStream();
	std::string stopPreviewLiveStream();
	void setExposureSettings(
		int bias = 0,
		double shutterSpeed = 1.0 / 120.0,
		int iso = 800,
		ins_camera::PhotographyOptions_ExposureMode exposureMode = ins_camera::PhotographyOptions_ExposureMode::PhotographyOptions_ExposureOptions_Program_AUTO,
		ins_camera::CameraFunctionMode functionMode = ins_camera::CameraFunctionMode::FUNCTION_MODE_NORMAL_IMAGE
	);
	std::shared_ptr<ins_camera::ExposureSettings> getExposureSettings(ins_camera::CameraFunctionMode functionMode);
	//  ## available settings and value range:
	// 	 * - Contrast: 0~256, default 64
	// 	 * - Saturation:0~256, default 64
	// 	 * - Brightness:-256~256, default 0
	// 	 * - WhiteBalance: see #PhotographyOptions_WhiteBalance
	// 	 * - Sharpness: 0~6, default 3
	void setCaptureSettings(
		int contrast = 64,
		int saturation = 64,
		int brightness = 0,
		int sharpness = 3,
		ins_camera::PhotographyOptions_WhiteBalance wbValue = ins_camera::PhotographyOptions_WhiteBalance_WB_AUTO,
		ins_camera::CameraFunctionMode functionMode = ins_camera::CameraFunctionMode::FUNCTION_MODE_NORMAL_IMAGE
	);
	std::shared_ptr<ins_camera::CaptureSettings> getCaptureSettings(ins_camera::CameraFunctionMode functionMode);
	std::string getUUID();
	void takePhotoAndDownload();
	json getCurrentCaptureStatus();
	void startTimelapse();
	void stopTimelapse();
	json getBatteryStatus();
	json getStorageInfo();

	// More camera-related methods...

private:
	std::shared_ptr<ins_camera::Camera> mCamera;
	std::vector<std::string> mSerialNumbersVec;
	void discoverAndOpenCamera();
	std::string mSerialNumber;
	size_t mCurrentDownloadIndex;
};
