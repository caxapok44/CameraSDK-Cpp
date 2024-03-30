#include "leticoCamera.h"

#include <string>
#include <vector>

LeticoCamera::LeticoCamera(): mCurrentDownloadIndex(0)
{
	discoverAndOpenCamera();
}

LeticoCamera::~LeticoCamera()
{
	if (mCamera)
	{
		mCamera->Close();
	}
}

std::string LeticoCamera::takePhoto()
{
	if (!mCamera)
	{
		std::cerr << "Camera not initialized." << std::endl;
		return "";
	}

	const auto url = mCamera->TakePhoto();
	if (!url.IsSingleOrigin() || url.Empty())
	{
		std::cerr << "Failed to take picture." << std::endl;
		return "";
	}

	std::cout << "Take picture done: " << url.GetSingleOrigin() << std::endl;
	return url.GetSingleOrigin();
}

std::string LeticoCamera::getSerialNumber()
{
	const auto serial_number = mCamera->GetSerialNumber();
	if (serial_number.empty())
	{
		std::cout << "failed to get serial number" << std::endl;
	}
	std::cout << "serial number: " << serial_number << std::endl;
	return serial_number;
}

std::vector<std::string> LeticoCamera::getAllSerialNumbers()
{
	return mSerialNumbersVec;
}
std::vector<std::string> LeticoCamera::getFileList()
{
	const auto file_list = mCamera->GetCameraFilesList();
	for (const auto &file : file_list)
	{
		std::cout << "File: " << file << std::endl;
	}
	return file_list;
}

void LeticoCamera::deleteFile(const std::string &file_to_delete)
{
	const auto ret = mCamera->DeleteCameraFile(file_to_delete);
	if (ret)
	{
		std::cout << "Deletion succeed" << std::endl;
	}
}

std::string LeticoCamera::downloadFile(std::string file_to_download)
{
	std::string file_to_save = Utils::getSavePath() / std::filesystem::path(file_to_download).filename();
	std::cout << file_to_download << " will be saved to " << file_to_save << std::endl;
	const auto ret = mCamera->DownloadCameraFile(
		file_to_download,
		file_to_save,
		[](int64_t current, int64_t total_size)
		{ std::cout << "current :" << current << "; total_size: " << total_size << std::endl; }
	);
	if (ret)
	{
		std::cout << "Download " << file_to_download << " succeed!!!" << std::endl;
		return file_to_save;
	}

	std::cout << "Download " << file_to_download << " failed!!!" << std::endl;
	return "";
}

std::vector<std::string> LeticoCamera::downloadFile(const std::vector<std::string> &files_to_download)
{
	std::vector<std::string> returnVector{};
	returnVector.reserve(files_to_download.size());
	for (const auto &file : files_to_download)
	{
		returnVector.push_back(downloadFile(file));
	}
	return returnVector;
}

void LeticoCamera::downloadAllFiles()
{
	std::string file_to_save = Utils::getSavePath();
	auto allFiles = getFileList();
	for (size_t i = 0; i < allFiles.size(); i++)
	{
		const auto ret = mCamera->DownloadCameraFile(allFiles[i], file_to_save + std::to_string(i));
		if (ret)
		{
			std::cout << "Download " << file_to_save + std::to_string(i) << " succeed!!!" << std::endl;
		}
		else
		{
			std::cout << "Download " << file_to_save + std::to_string(i) << " failed!!!" << std::endl;
		}
	}
}

std::string LeticoCamera::startPreviewLiveStream()
{
	ins_camera::LiveStreamParam param;
	param.video_resolution = ins_camera::VideoResolution::RES_720_360P30;
	param.lrv_video_resulution = ins_camera::VideoResolution::RES_720_360P30;
	param.video_bitrate = 1024 * 1024 / 2;
	param.enable_audio = false;
	param.using_lrv = false;
	if (mCamera->StartLiveStreaming(param))
	{
		std::cout << "successfully started live stream" << std::endl;
		return "";
	}

	return "Failed to start stream";
}

std::string LeticoCamera::stopPreviewLiveStream()
{
	if (mCamera->StopLiveStreaming())
	{
		std::cout << "success!" << std::endl;
		return "";
	}

	std::cerr << "failed to stop live." << std::endl;
	return "Failed to stop live stream";
}

void LeticoCamera::setExposureSettings()
{
	auto exposure_settings = mCamera->GetExposureSettings(ins_camera::CameraFunctionMode::FUNCTION_MODE_NORMAL_IMAGE);
	if (exposure_settings)
	{
		std::cout << "EVBias : " << exposure_settings->EVBias() << std::endl;
		std::cout << "ISO    : " << exposure_settings->Iso() << std::endl;
		std::cout << "speed  : " << exposure_settings->ShutterSpeed() << std::endl;
	}

	int bias;
	std::cout << "please enter EVBIOS: ";
	std::cin >> bias;
	auto exposure = std::make_shared<ins_camera::ExposureSettings>();
	auto exposure_mode = ins_camera::PhotographyOptions_ExposureMode::PhotographyOptions_ExposureOptions_Program_AUTO;
	if (mCamera->GetCameraType() == ins_camera::CameraType::Insta360X3)
	{
		exposure_mode = ins_camera::PhotographyOptions_ExposureMode::PhotographyOptions_ExposureOptions_Program_FULL_AUTO;
	}
	exposure->SetExposureMode(exposure_mode);
	exposure->SetEVBias(bias);	// range -80 ~ 80, default 0, step 1
	exposure->SetIso(800);
	exposure->SetShutterSpeed(1.0 / 120.0);
	auto ret = mCamera->SetExposureSettings(ins_camera::CameraFunctionMode::FUNCTION_MODE_NORMAL_IMAGE, exposure);
	if (ret)
	{
		auto exposure_settings = mCamera->GetExposureSettings(ins_camera::CameraFunctionMode::FUNCTION_MODE_NORMAL_IMAGE);
		std::cout << "success! ISO " << exposure_settings->Iso() << ", WB = " << exposure_settings->ShutterSpeed()
				  << ", ExposureMode = " << exposure_settings->ExposureMode() << std::endl;
	}
	else
	{
		std::cerr << "failed to set exposure" << std::endl;
	}
}
void LeticoCamera::setCaptureSettings()
{
	auto settings = std::make_shared<ins_camera::CaptureSettings>();
	settings->SetValue(ins_camera::CaptureSettings::CaptureSettings_Saturation, 0);
	settings->SetWhiteBalance(ins_camera::PhotographyOptions_WhiteBalance_WB_4000K);
	settings->SetValue(ins_camera::CaptureSettings::CaptureSettings_Brightness, 100);
	auto ret = mCamera->SetCaptureSettings(ins_camera::CameraFunctionMode::FUNCTION_MODE_NORMAL_IMAGE, settings);
	if (ret)
	{
		auto capture_settings = mCamera->GetCaptureSettings(ins_camera::CameraFunctionMode::FUNCTION_MODE_NORMAL_IMAGE);
		std::cout << "success!" << std::endl;
	}
	else
	{
		std::cerr << "failed to set capture settings" << std::endl;
	}
}

std::string LeticoCamera::getUUID()
{
	auto str_uuid = mCamera->GetCameraUUID();
	if (str_uuid.empty())
	{
		std::cerr << "failed to get uuid" << std::endl;
		return "";
	}
	std::cout << "uuid : " << str_uuid << std::endl;
	return str_uuid;
}

void LeticoCamera::takePhotoAndDownload()
{
	while (1)
	{
		const auto url = mCamera->TakePhoto();
		if (!url.IsSingleOrigin() || url.Empty())
		{
			std::cout << "failed to take picture" << std::endl;
		}

		std::string download_url = url.GetSingleOrigin();
		std::string save_path = "D:/testImage" + download_url;
		const auto ret = mCamera->DownloadCameraFile(download_url, save_path);
		if (ret)
		{
			std::cout << "Download " << download_url << " succeed!!!" << std::endl;
		}
		else
		{
			std::cout << "Download " << download_url << " failed!!!" << std::endl;
		}

		std::this_thread::sleep_for(std::chrono::microseconds(1));
	}
	mCamera->Close();
}

json LeticoCamera::getCurrentCaptureStatus()
{
	auto ret = mCamera->GetCaptureCurrentStatus();
	if (ret == ins_camera::CaptureStatus::NOT_CAPTURE)
	{
		std::cout << "current statue : not capture" << std::endl;
	}
	else
	{
		std::cout << "current statue : capture" << std::endl;
	}
	json result;
	switch(ret) {
        case ins_camera::CaptureStatus::NOT_CAPTURE:
            result = {{"status", "not capture"}};
            break;
        case ins_camera::CaptureStatus::NORMAL_CAPTURE:
            result = {{"status", "normal capture"}};
            break;
        case ins_camera::CaptureStatus::TIMELAPSE_CAPTURE:
            result = {{"status", "timelapse capture"}};
            break;
        case ins_camera::CaptureStatus::INTERVAL_SHOOTING_CAPTURE:
            result = {{"status", "interval shooting capture"}};
            break;
        case ins_camera::CaptureStatus::SINGLE_SHOOTING:
            result = {{"status", "single shooting"}};
            break;
        case ins_camera::CaptureStatus::HDR_SHOOTING:
            result = {{"status", "HDR shooting"}};
            break;
		case ins_camera::CaptureStatus::SELF_TIMER_SHOOTING:
            result = {{"status", "SELF TIMER SHOOTING"}};
            break;
        case ins_camera::CaptureStatus::BULLET_TIME_CAPTURE:
            result = {{"status", "BULLET TIME CAPTURE"}};
            break;
        case ins_camera::CaptureStatus::SETTINGS_NEW_VALUE:
            result = {{"status", "SETTINGS NEW VALUE"}};
            break;
        case ins_camera::CaptureStatus::HDR_CAPTURE:
            result = {{"status", "HDR CAPTURE"}};
            break;
        case ins_camera::CaptureStatus::BURST_SHOOTING:
            result = {{"status", "BURST SHOOTING"}};
            break;
		case ins_camera::CaptureStatus::STATIC_TIMELAPSE_SHOOTING:
            result = {{"status", "STATIC TIMELAPSE SHOOTING"}};
            break;
        case ins_camera::CaptureStatus::INTERVAL_VIDEO_CAPTURE:
            result = {{"status", "INTERVAL VIDEO CAPTURE"}};
            break;
        case ins_camera::CaptureStatus::TIMESHIFT_CAPTURE:
            result = {{"status", "TIMESHIFT CAPTURE"}};
            break;
        case ins_camera::CaptureStatus::AEB_NIGHT_SHOOTING:
            result = {{"status", "AEB NIGHT SHOOTING"}};
            break;
        case ins_camera::CaptureStatus::SINGLE_POWER_PANO_SHOOTING:
            result = {{"status", "SINGLE POWER PANO SHOOTING"}};
            break;
		case ins_camera::CaptureStatus::HDR_POWER_PANO_SHOOTING:
            result = {{"status", "HDR POWER PANO SHOOTING"}};
            break;
        case ins_camera::CaptureStatus::SUPER_NORMAL_CAPTURE:
            result = {{"status", "SUPER NORMAL CAPTURE"}};
            break;
        case ins_camera::CaptureStatus::LOOP_RECORDING_CAPTURE:
            result = {{"status", "LOOP RECORDING CAPTURE"}};
            break;
        case ins_camera::CaptureStatus::STARLAPSE_SHOOTING:
            result = {{"status", "STARLAPSE SHOOTING"}};
            break;
        default:
            result = {{"status", "unknown"}};
            break;
    }
	return result;
}

void LeticoCamera::startTimelapse()
{
	ins_camera::RecordParams record_params;
	record_params.resolution = ins_camera::VideoResolution::RES_3920_1920P30;
	if (!mCamera->SetVideoCaptureParams(record_params, ins_camera::CameraFunctionMode::FUNCTION_MODE_MOBILE_TIMELAPSE))
	{
		std::cerr << "failed to set capture settings." << std::endl;
	}

	// mode 是你相机所支持的模式
	ins_camera::TimelapseParam param = {ins_camera::CameraTimelapseMode::MOBILE_TIMELAPSE_VIDEO, 0, 1000, 5};
	if (!mCamera->SetTimeLapseOption(param))
	{
		std::cerr << "failed to set capture settings." << std::endl;
	}
	else
	{
		auto ret = mCamera->StartTimeLapse(param.mode);
		if (ret)
		{
			std::cerr << "success!" << std::endl;
		}
		else
		{
			std::cerr << "failed to start timelapse" << std::endl;
		}
	}
}

void LeticoCamera::stopTimelapse()
{
	auto url = mCamera->StopTimeLapse(ins_camera::CameraTimelapseMode::MOBILE_TIMELAPSE_VIDEO);
	if (url.Empty())
	{
		std::cerr << "stop timelapse failed" << std::endl;
	}
	auto &origins = url.OriginUrls();
	std::cout << "stop timelapse success" << std::endl;
	for (auto &origin_url : origins)
	{
		std::cout << "url:" << origin_url << std::endl;
	}
}

json LeticoCamera::getBatteryStatus()
{
	json result;

	if (!mCamera->IsConnected())
	{
		result["error"] = "Device is offline";
		return result;
	}

	ins_camera::BatteryStatus status;
	bool ret = mCamera->GetBatteryStatus(status);

	if (!ret)
	{
		result["error"] = "GetBatteryStatus failed";
		return result;
	}

	std::string powerType = status.power_type == ins_camera::PowerType::BATTERY ? "Battery" : "Adapter";

	result["PowerType"] = powerType;
	result["battery_level"] = status.battery_level;
	result["battery_scale"] = status.battery_scale;

	return result;
}

json LeticoCamera::getStorageInfo()
{
	json result;

	ins_camera::StorageStatus status;
	bool ret = mCamera->GetStorageState(status);
	if (!ret)
	{
		result["error"] = "GetStorageState failed";
		return result;
	}
	std::cout << "free_space : " << status.free_space / 1000000000.0 << "GB" << std::endl;
	std::cout << "total_space : " << status.total_space / 1000000000.0 << "GB" << std::endl;
	std::cout << "state : " << status.state << std::endl;

	result["free_space_GB"] = status.free_space / 1000000000.0;	 // Convert to GB
	result["total_space_GB"] = status.total_space / 1000000000.0;  // Convert to GB

	switch (status.state)
	{
	case ins_camera::CardState::STOR_CS_PASS:
		result["state"] = "Pass";
		break;
	case ins_camera::CardState::STOR_CS_NOCARD:
		result["state"] = "No Card";
		break;
	case ins_camera::CardState::STOR_CS_NOSPACE:
		result["state"] = "No Space";
		break;
	case ins_camera::CardState::STOR_CS_INVALID_FORMAT:
		result["state"] = "Invalid Format";
		break;
	case ins_camera::CardState::STOR_CS_WPCARD:
		result["state"] = "Write Protected Card";
		break;
	case ins_camera::CardState::STOR_CS_OTHER_ERROR:
		result["state"] = "Other Error";
		break;
	default:
		result["state"] = "Unknown State";
		break;
	}

	return result;
}

std::string LeticoCamera::startRecording()
{
	auto errorMessage = "";
	if (!mCamera)
	{
		errorMessage = "Camera not initialized.";
		std::cerr << errorMessage << std::endl;
		return errorMessage;
	}

	ins_camera::RecordParams record_params;
	record_params.resolution = ins_camera::VideoResolution::RES_3040_3040P24;
	record_params.bitrate = 1024 * 1024 * 10;  // Example bitrate setting

	if (!mCamera->SetVideoCaptureParams(record_params, ins_camera::CameraFunctionMode::FUNCTION_MODE_NORMAL_VIDEO))
	{
		errorMessage = "Failed to set capture settings.";
		std::cerr << errorMessage << std::endl;
		return errorMessage;
	}

	if (mCamera->StartRecording())
	{
		std::cout << "Recording started successfully." << std::endl;
		return errorMessage;
	}
	errorMessage = "Failed to start recording.";
	std::cerr << errorMessage << std::endl;
	return errorMessage;
}

std::vector<std::string> LeticoCamera::stopRecording()
{
	if (!mCamera)
	{
		std::cerr << "Camera not initialized." << std::endl;
		return {};
	}

	auto url = mCamera->StopRecording();
	if (url.Empty())
	{
		std::cerr << "Stop recording failed." << std::endl;
		return {};
	}

	std::cout << "Recording stopped successfully." << std::endl;
	for (auto &origin_url : url.OriginUrls())
	{
		std::cout << "URL: " << origin_url << std::endl;
	}
	return url.OriginUrls();
}

void LeticoCamera::discoverAndOpenCamera()
{
	std::cout << "begin open camera" << std::endl;
	ins_camera::DeviceDiscovery discovery;
	auto list = discovery.GetAvailableDevices();
	for (const auto &desc : list)
	{
		mSerialNumbersVec.push_back(desc.serial_number);
	}
	if (!list.empty())
	{
		mCamera = std::make_shared<ins_camera::Camera>(list[0].info);
	}
	else
	{
		std::cerr << "No camera devices found." << std::endl;
	}
	if (mCamera && !mCamera->Open())
	{
		std::cerr << "Failed to open camera" << std::endl;
		mCamera.reset();
	}
	else
	{
		std::cout << "Camera opened successfully." << std::endl;
	}
	mSerialNumber = mCamera->GetSerialNumber();
	std::cout << mCamera->GetHttpBaseUrl();
	discovery.FreeDeviceDescriptors(list);
}
