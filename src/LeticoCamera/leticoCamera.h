#pragma once

#include <camera/camera.h>
#include <camera/device_discovery.h>
#include <camera/photography_settings.h>

#include "../utils.hpp"

#include <iostream>
#include <string>
#include <thread>
#include <vector>


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
	std::vector<std::string> downloadFile(const std::vector<std::string> &files_to_download);

	void downloadAllFiles();

	std::string startRecording();
	std::vector<std::string> stopRecording();
	void startPreviewLiveStream();
	void stopPreviewLiveStream();
	void setExposureSettings();
	void setCaptureSettings();
	void getUUID();
	void takePhotoAndDownload();
	void getCurrentCaptureStatus();
	void startTimelapse();
	void stopTimelapse();
	void getBatteryStatus();
	void getStorageInfo();

	// More camera-related methods...

private:
	std::shared_ptr<ins_camera::Camera> mCamera;
	std::vector<std::string> mSerialNumbersVec;
	void discoverAndOpenCamera();
	std::string mSerialNumber;
	size_t mCurrentDownloadIndex;
};
