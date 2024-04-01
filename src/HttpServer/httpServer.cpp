#pragma once
#include "httpServer.h"

#include <cstddef>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

#include "camera/photography_settings.h"

using namespace Letico;

LeticoHttpServer::LeticoHttpServer(std::vector<std::shared_ptr<LeticoCamera>> cameras):
	mCameras(std::move(cameras)), mServerAddress("localhost"), mPort(9091)
{
	mServer = std::make_shared<httplib::Server>();
	createServer();
}
LeticoHttpServer::~LeticoHttpServer()
{
	if (mServer != nullptr)
		mServer->stop();
	if (mServerThread.joinable())
		mServerThread.join();
}

void LeticoHttpServer::createServer()
{
	mServer->set_base_dir(".");
	mServer->set_mount_point("/", ".");
	mServer->set_pre_routing_handler(
		[](const httplib::Request&, httplib::Response& res)
		{
			res.set_header("Access-Control-Allow-Origin", "*");
			res.set_header("Access-Control-Allow-Headers", "Origin, X-Requested-With, Content-Type, Accept");
			return httplib::Server::HandlerResponse::Unhandled;
		}
	);

	createEndpoints();

	mServerThread = std::thread([&] { mServer->listen(mServerAddress, mPort); });
	const int RECONNECT_RETRIES_AMOUNT = 1000;
	for (int i = 0; i < RECONNECT_RETRIES_AMOUNT; i++)
	{
		std::this_thread::sleep_for(std::chrono::milliseconds(2));
		if (mServer->is_running())
		{
			std::cout << "HTTP server listen on port " << mPort;
			break;
		}
	}
}

void LeticoHttpServer::createEndpoints()
{
	//======== HEALTHY ===========
	mServer->Get(
		"/api/v1/healthy", [&](const httplib::Request&, httplib::Response& res) { res.set_content("Healthy", "text/plain"); }
	);
	//======== Live stream ===========

	mServer->Get(
		"/stream.m3u8",
		[](const httplib::Request&, httplib::Response& res)
		{
			std::ifstream t("stream.m3u8");
			std::stringstream buffer;
			buffer << t.rdbuf();

			res.set_content(buffer.str(), "application/x-mpegURL");
		}
	);

	mServer->Get(
		R"(/(.+\.ts))",
		[](const httplib::Request& req, httplib::Response& res)
		{
			std::ifstream file(req.matches[1], std::ios::binary | std::ios::ate);
			if (file.is_open())
			{
				auto size = file.tellg();
				std::string buffer(size,
								   '\0');  // Create string buffer to the size of the file
				file.seekg(0);
				file.read(buffer.data(), size);

				res.set_content(buffer, "video/MP2T");
			}
			else
			{
				res.status = NOT_FOUND;
			}
		}
	);
	//======== Get all cameras series ========
	mServer->Get(
		"/api/v1/serialNumbers",
		[&](const httplib::Request& req, httplib::Response& res)
		{
			nlohmann::json jsonResponse;

			if (mCameras.empty())
			{
				jsonResponse = {{"status", "error"}, {"message", "No initialized cameras"}};
				res.set_content(jsonResponse.dump(), "application/json");
				return;
			}
			auto serialNumbers = mCameras[0]->getAllSerialNumbers();

			nlohmann::json jsonSerialNumbers = nlohmann::json::array();
			for (const auto& serialNumber : serialNumbers)
			{
				jsonSerialNumbers.push_back(serialNumber);
			}

			jsonResponse = {{"status", "success"}, {"serialNumbers", jsonSerialNumbers}};

			res.set_content(jsonResponse.dump(), "application/json");
		}
	);
	// ============ UPLOAD MEDIA ==============
	mServer->Post(
		"/api/v1/getMedia",
		[](const httplib::Request& req, httplib::Response& res)
		{
			// Extract the filename from the request. You might send it as a form field or JSON.
			// Here, it's expected as a form field for simplicity.
			auto fileName = req.get_param_value("fileName");
			nlohmann::json jsonResponse;

			// Check if the file exists and is not a directory
			std::ifstream fileStream(fileName, std::ios::binary | std::ios::ate);
			if (!fileStream.is_open())
			{
				// File not found
				res.status = NOT_FOUND;
				jsonResponse = {{"status", "error"}, {"message", "File not found"}};
				res.set_content(jsonResponse.dump(), "application/json");
				return;
				return;
			}

			// Get the file size
			auto fileSize = fileStream.tellg();
			fileStream.seekg(0, std::ios::beg);

			// Read the file content into a string
			std::vector<char> fileContent(fileSize);
			fileStream.read(fileContent.data(), fileSize);

			// Set appropriate MIME type (for simplicity, only distinguishing between images and videos here)
			std::string mimeType = "application/octet-stream";	// Default MIME type
			if (fileName.find(".insp") != std::string::npos || fileName.find(".jpg") != std::string::npos ||
				fileName.find(".jpeg") != std::string::npos || fileName.find(".png") != std::string::npos)
			{
				mimeType = "image/jpeg";  // Assuming JPEG for all images
			}
			else if (fileName.find(".lrv") != std::string::npos || fileName.find(".insv") != std::string::npos || fileName.find(".mp4") != std::string::npos)
			{
				mimeType = "video/mp4";
			}

			// Respond with the file content
			res.set_content(fileContent.data(), fileSize, mimeType);
		}
	);

	//========  Take photo ===========
	mServer->Post(
		"/api/v1/takePhoto",
		[&](const httplib::Request& req, httplib::Response& res)
		{
			size_t cameraIndex = std::stoi(req.get_param_value("cameraIndex"));
			nlohmann::json jsonResponse;
			if (cameraIndex < 0 || cameraIndex >= mCameras.size())
			{
				jsonResponse = {{"status", "error"}, {"message", "Invalid camera index"}};
				res.status = BAD_REQUEST;
				res.set_content(jsonResponse.dump(), "application/json");
				return;
			}

			auto deviceUrl = mCameras[cameraIndex]->takePhoto();
			if (deviceUrl.empty())
			{
				jsonResponse = {{"status", "error"}, {"message", "Something went wrong, unable to take photo"}};
				res.set_content(jsonResponse.dump(), "application/json");
				return;
			}
			std::string folderUrl = mCameras[cameraIndex]->downloadFile(deviceUrl);
			if (folderUrl.empty())
			{
				jsonResponse = {{"status", "error"}, {"message", "Taken photo failed"}};
				res.set_content(jsonResponse.dump(), "application/json");
				return;
			}
			std::string wwwUrl = mServerAddress + ":" + std::to_string(mPort) + "/api/v1/getMedia?fileName=" + folderUrl;
			jsonResponse = {
				{"status", "success"},
				{"message", "Photo taken successfully"},
				{"deviceUrl", deviceUrl},
				{"homeUrl", folderUrl},
				{"wwwUrl", wwwUrl}};

			res.set_content(jsonResponse.dump(), "application/json");
		}
	);

	//======== getSerialNumber ===========
	mServer->Post(
		"/api/v1/getSerialNumber",
		[&](const httplib::Request& req, httplib::Response& res)
		{
			nlohmann::json jsonResponse;

			try
			{
				size_t cameraIndex = std::stoi(req.get_param_value("cameraIndex"));
				if (cameraIndex < 0 || cameraIndex >= mCameras.size())
				{
					throw std::out_of_range("Invalid camera index");
				}
				std::string serialNumber = mCameras[cameraIndex]->getSerialNumber();
				jsonResponse = {{"status", "success"}, {"serialNumber", serialNumber}};
			}
			catch (const std::exception& e)
			{
				jsonResponse = {{"status", "error"}, {"message", e.what()}};
				res.status = BAD_REQUEST;
			}

			res.set_content(jsonResponse.dump(), "application/json");
		}
	);
	//======== Get list of files ========
	mServer->Post(
		"/api/v1/getFileLists",
		[&](const httplib::Request& req, httplib::Response& res)
		{
			nlohmann::json jsonResponse;

			try
			{
				size_t cameraIndex = std::stoi(req.get_param_value("cameraIndex"));
				if (cameraIndex < 0 || cameraIndex >= mCameras.size())
				{
					throw std::out_of_range("Invalid camera index");
				}
				auto fileLists = mCameras[cameraIndex]->getFileList();
				jsonResponse = {{"status", "success"}, {"files", fileLists}};
			}
			catch (const std::exception& e)
			{
				jsonResponse = {{"status", "error"}, {"message", e.what()}};
				res.status = BAD_REQUEST;
			}

			res.set_content(jsonResponse.dump(), "application/json");
		}
	);
	//======== Start Recording ========
	mServer->Post(
		"/api/v1/startRecording",
		[&](const httplib::Request& req, httplib::Response& res)
		{
			size_t cameraIndex = 0;
			nlohmann::json jsonResponse;
			try
			{
				cameraIndex = std::stoi(req.get_param_value("cameraIndex"));
			}
			catch (...)
			{
				jsonResponse = {{"status", "error"}, {"message", "Camera index is required"}};
				res.status = BAD_REQUEST;
				res.set_content(jsonResponse.dump(), "application/json");
				return;
			}

			if (cameraIndex >= mCameras.size())
			{
				jsonResponse = {{"status", "error"}, {"message", "Invalid camera index"}};
				res.status = BAD_REQUEST;
				res.set_content(jsonResponse.dump(), "application/json");
				return;
			}

			// Default values for optional parameters
			int bitrate = req.has_param("bitrate") ? std::stoi(req.get_param_value("bitrate")) : 1024 * 1024 * 10;
			auto resolution = req.has_param("resolution")
								  ? static_cast<ins_camera::VideoResolution>(std::stoi(req.get_param_value("resolution")))
								  : ins_camera::VideoResolution::RES_3040_3040P24;
			auto functionMode = req.has_param("functionMode")
									? static_cast<ins_camera::CameraFunctionMode>(std::stoi(req.get_param_value("functionMode")))
									: ins_camera::CameraFunctionMode::FUNCTION_MODE_NORMAL_VIDEO;

			auto errorMessage = mCameras[cameraIndex]->startRecording(bitrate, resolution, functionMode);
			if (!errorMessage.empty())
			{
				jsonResponse = {{"status", "error"}, {"message", errorMessage}};
				res.status = INTERNAL_SERVER_ERROR;	 // Internal Server Error
			}
			else
			{
				jsonResponse = {{"status", "success"}, {"message", "Started recording successfully"}};
				res.status = OK;
			}

			res.set_content(jsonResponse.dump(), "application/json");
		}
	);
	//======== Stop Recording ========
	mServer->Post(
		"/api/v1/stopRecording",
		[&](const httplib::Request& req, httplib::Response& res)
		{
			size_t cameraIndex = std::stoi(req.get_param_value("cameraIndex"));
			nlohmann::json jsonResponse;

			if (cameraIndex < 0 || cameraIndex >= mCameras.size())
			{
				jsonResponse = {{"status", "error"}, {"message", "Invalid camera index"}};
				res.status = BAD_REQUEST;
				res.set_content(jsonResponse.dump(), "application/json");
				return;
			}

			auto deviceUrls = mCameras[cameraIndex]->stopRecording();
			if (deviceUrls.empty())
			{
				jsonResponse = {{"status", "error"}, {"message", "Unable to stop recording, no recordings found"}};
				res.set_content(jsonResponse.dump(), "application/json");
				return;
			}

			auto folderUrls = mCameras[cameraIndex]->downloadFile(deviceUrls);
			std::vector<std::string> wwwUrls;
			wwwUrls.reserve(folderUrls.size());
			for (auto folderUrl : folderUrls)
			{
				wwwUrls.push_back(mServerAddress + ":" + std::to_string(mPort) + "/api/v1/getMedia?fileName=" + folderUrl);
			}
			jsonResponse = {
				{"status", "success"},
				{"message", "Recording stopped and files downloaded successfully"},
				{"deviceUrls", deviceUrls},
				{"homeUrls", folderUrls},
				{"wwwUrls", wwwUrls}};

			res.set_content(jsonResponse.dump(), "application/json");
		}
	);
	//======== Delete file from device ========
	mServer->Post(
		"/api/v1/deleteFile",
		[&](const httplib::Request& req, httplib::Response& res)
		{
			size_t cameraIndex = std::stoi(req.get_param_value("cameraIndex"));
			nlohmann::json jsonResponse;

			if (cameraIndex < 0 || cameraIndex >= mCameras.size())
			{
				jsonResponse = {{"status", "error"}, {"message", "Invalid camera index"}};
				res.status = BAD_REQUEST;
				res.set_content(jsonResponse.dump(), "application/json");
				return;
			}

			std::string fileToDelete = req.get_param_value("fileToDelete");
			mCameras[cameraIndex]->deleteFile(fileToDelete);

			jsonResponse = {{"status", "success"}, {"message", "Deletion successful"}, {"deletedFile", fileToDelete}};

			res.set_content(jsonResponse.dump(), "application/json");
		}
	);
	//======== Download file from device========
	mServer->Post(
		"/api/v1/downloadFile",
		[&](const httplib::Request& req, httplib::Response& res)
		{
			size_t cameraIndex = std::stoi(req.get_param_value("cameraIndex"));
			nlohmann::json jsonResponse;

			if (cameraIndex < 0 || cameraIndex >= mCameras.size())
			{
				jsonResponse = {{"status", "error"}, {"message", "Invalid camera index"}};
				res.status = BAD_REQUEST;
				res.set_content(jsonResponse.dump(), "application/json");
				return;
			}

			std::string fileToDownload = req.get_param_value("fileToDownload");
			auto folderUrl = mCameras[cameraIndex]->downloadFile(fileToDownload);
			jsonResponse = {
				{"status", "success"},
				{"message", "File downloaded successfully"},
				{"deviceUrl", fileToDownload},
				{"folderUrl", folderUrl}};

			res.set_content(jsonResponse.dump(), "application/json");
		}
	);
	//======== Start live stream ======== // Stream delegate should be set
	mServer->Post(
		"/api/v1/startLiveStream",
		[&](const httplib::Request& req, httplib::Response& res)
		{
			size_t cameraIndex = std::stoi(req.get_param_value("cameraIndex"));
			nlohmann::json jsonResponse;

			if (cameraIndex < 0 || cameraIndex >= mCameras.size())
			{
				jsonResponse = {{"status", "error"}, {"message", "Invalid camera index"}};
				res.status = BAD_REQUEST;
				res.set_content(jsonResponse.dump(), "application/json");
				return;
			}

			auto errorMessage = mCameras[cameraIndex]->startPreviewLiveStream();
			if (!errorMessage.empty())
			{
				jsonResponse = {{"status", "error"}, {"message", errorMessage}};
				res.status = INTERNAL_SERVER_ERROR;	 // Internal error
				res.set_content(jsonResponse.dump(), "application/json");
				return;
			}

			jsonResponse = {{"status", "success"}, {"message", "Start streaming successfully"}};

			res.set_content(jsonResponse.dump(), "application/json");
		}
	);
	//======== Stop live stream ========
	mServer->Post(
		"/api/v1/stopLiveStream",
		[&](const httplib::Request& req, httplib::Response& res)
		{
			size_t cameraIndex = std::stoi(req.get_param_value("cameraIndex"));
			nlohmann::json jsonResponse;

			if (cameraIndex < 0 || cameraIndex >= mCameras.size())
			{
				jsonResponse = {{"status", "error"}, {"message", "Invalid camera index"}};
				res.status = BAD_REQUEST;
				res.set_content(jsonResponse.dump(), "application/json");
				return;
			}

			auto errorMessage = mCameras[cameraIndex]->stopPreviewLiveStream();
			if (!errorMessage.empty())
			{
				jsonResponse = {{"status", "error"}, {"message", errorMessage}};
				res.set_content(jsonResponse.dump(), "application/json");
				return;
			}

			jsonResponse = {{"status", "success"}, {"message", "Stop streaming successfully"}};

			res.set_content(jsonResponse.dump(), "application/json");
		}
	);
	//======== get battery info ========
	mServer->Post(
		"/api/v1/getBatteryInfo",
		[&](const httplib::Request& req, httplib::Response& res)
		{
			size_t cameraIndex = std::stoi(req.get_param_value("cameraIndex"));
			nlohmann::json jsonResponse;

			if (cameraIndex < 0 || cameraIndex >= mCameras.size())
			{
				jsonResponse = {{"status", "error"}, {"message", "Invalid camera index"}};
				res.status = BAD_REQUEST;
				res.set_content(jsonResponse.dump(), "application/json");
				return;
			}

			auto resultJson = mCameras[cameraIndex]->getBatteryStatus();
			if (resultJson.contains("error"))
			{
				jsonResponse = {{"status", "error"}, {"message", resultJson["error"]}};
				res.status = INTERNAL_SERVER_ERROR;	 // Internal Server Error
				res.set_content(jsonResponse.dump(), "application/json");
				return;
			}

			jsonResponse = {{"status", "success"}, {"data", resultJson}};
			res.status = OK;  // OK

			res.set_content(jsonResponse.dump(), "application/json");
		}
	);
	//======== get storage info ========

	mServer->Post(
		"/api/v1/getStorageInfo",
		[&](const httplib::Request& req, httplib::Response& res)
		{
			size_t cameraIndex = std::stoi(req.get_param_value("cameraIndex"));
			nlohmann::json jsonResponse;

			if (cameraIndex >= mCameras.size())
			{
				jsonResponse = {{"status", "error"}, {"message", "Invalid camera index"}};
				res.status = BAD_REQUEST;
				res.set_content(jsonResponse.dump(), "application/json");
				return;
			}

			auto resultJson = mCameras[cameraIndex]->getStorageInfo();
			if (resultJson.contains("error"))
			{
				jsonResponse = {{"status", "error"}, {"message", resultJson["error"]}};
				res.status = INTERNAL_SERVER_ERROR;	 // Internal Server Error
				res.set_content(jsonResponse.dump(), "application/json");
				return;
			}

			jsonResponse = {{"status", "success"}, {"data", resultJson}};
			res.status = OK;  // OK

			res.set_content(jsonResponse.dump(), "application/json");
		}
	);
	//======== get uuid ========
	mServer->Post(
		"/api/v1/getUUID",
		[&](const httplib::Request& req, httplib::Response& res)
		{
			size_t cameraIndex = std::stoi(req.get_param_value("cameraIndex"));
			nlohmann::json jsonResponse;

			if (cameraIndex >= mCameras.size())
			{
				jsonResponse = {{"status", "error"}, {"message", "Invalid camera index"}};
				res.status = BAD_REQUEST;
				res.set_content(jsonResponse.dump(), "application/json");
				return;
			}

			std::string uuid = mCameras[cameraIndex]->getUUID();
			if (uuid.empty())
			{
				jsonResponse = {{"status", "error"}, {"message", "Failed to get UUID"}};
				res.status = INTERNAL_SERVER_ERROR;	 // Internal Server Error
				res.set_content(jsonResponse.dump(), "application/json");
				return;
			}

			jsonResponse = {{"status", "success"}, {"uuid", uuid}};
			res.status = OK;  // OK

			res.set_content(jsonResponse.dump(), "application/json");
		}
	);
	//======== get current capture status ========
	mServer->Post(
		"/api/v1/getCurrentCaptureStatus",
		[&](const httplib::Request& req, httplib::Response& res)
		{
			size_t cameraIndex = std::stoi(req.get_param_value("cameraIndex"));
			nlohmann::json jsonResponse;

			if (cameraIndex >= mCameras.size())
			{
				jsonResponse = {{"status", "error"}, {"message", "Invalid camera index"}};
				res.status = BAD_REQUEST;
				res.set_content(jsonResponse.dump(), "application/json");
				return;
			}

			auto captureStatusJson = mCameras[cameraIndex]->getCurrentCaptureStatus();
			jsonResponse = {{"status", "success"}, {"data", captureStatusJson}};
			res.status = OK;  // OK

			res.set_content(jsonResponse.dump(), "application/json");
		}
	);
	//======== set exposure settings with optional parameters ========
	mServer->Post(
		"/api/v1/setExposureSettings",
		[&](const httplib::Request& req, httplib::Response& res)
		{
			size_t cameraIndex = 0;
			nlohmann::json jsonResponse;
			try
			{
				cameraIndex = std::stoi(req.get_param_value("cameraIndex"));
			}
			catch (...)
			{
				jsonResponse = {{"status", "error"}, {"message", "Camera index is required"}};
				res.status = BAD_REQUEST;
				res.set_content(jsonResponse.dump(), "application/json");
				return;
			}
			// Ensure the camera index is within the valid range
			if (cameraIndex >= mCameras.size())
			{
				jsonResponse = {{"status", "error"}, {"message", "Invalid camera index"}};
				res.status = BAD_REQUEST;
				res.set_content(jsonResponse.dump(), "application/json");
				return;
			}

			auto camera = mCameras[cameraIndex];
			auto defaultFunctionMode = ins_camera::CameraFunctionMode::FUNCTION_MODE_NORMAL_IMAGE;
			auto functionMode = req.has_param("functionMode")
									? static_cast<ins_camera::CameraFunctionMode>(std::stoi(req.get_param_value("functionMode")))
									: defaultFunctionMode;

			auto defaultExposureSettings = camera->getExposureSettings(functionMode);

			int defaultBias = defaultExposureSettings ? defaultExposureSettings->EVBias() : 0;
			double defaultShutterSpeed = defaultExposureSettings ? defaultExposureSettings->ShutterSpeed() : 1.0 / 120.0;
			int defaultIso = defaultExposureSettings ? defaultExposureSettings->Iso() : 800;
			auto defaultExposureMode =
				defaultExposureSettings
					? defaultExposureSettings->ExposureMode()
					: ins_camera::PhotographyOptions_ExposureMode::PhotographyOptions_ExposureOptions_Program_AUTO;

			int bias = req.has_param("bias") ? std::stoi(req.get_param_value("bias")) : defaultBias;
			double shutterSpeed = req.has_param("shutterSpeed") ? std::stod(req.get_param_value("shutterSpeed")) : defaultShutterSpeed;
			int iso = req.has_param("iso") ? std::stoi(req.get_param_value("iso")) : defaultIso;
			auto exposureMode =
				req.has_param("exposureMode")
					? static_cast<ins_camera::PhotographyOptions_ExposureMode>(std::stoi(req.get_param_value("exposureMode")))
					: defaultExposureMode;
			try
			{
				camera->setExposureSettings(bias, shutterSpeed, iso, exposureMode, functionMode);
				jsonResponse = {{"status", "success"}, {"message", "Exposure settings updated successfully"}};
				res.status = OK;  // OK
			}
			catch (const std::exception& e)
			{
				jsonResponse = {{"status", "error"}, {"message", "Failed to set exposure settings", "errorDetail", e.what()}};
				res.status = INTERNAL_SERVER_ERROR;	 // Internal Server Error
			}

			res.set_content(jsonResponse.dump(), "application/json");
		}
	);
	//======== set capture settings with optional parameters ========
	mServer->Post(
		"/api/v1/setCaptureSettings",
		[&](const httplib::Request& req, httplib::Response& res)
		{
			size_t cameraIndex = 0;
			nlohmann::json jsonResponse;
			try
			{
				cameraIndex = std::stoi(req.get_param_value("cameraIndex"));
			}
			catch (...)
			{
				jsonResponse = {{"status", "error"}, {"message", "Camera index is required"}};
				res.status = BAD_REQUEST;
				res.set_content(jsonResponse.dump(), "application/json");
				return;
			}

			if (cameraIndex >= mCameras.size())
			{
				jsonResponse = {{"status", "error"}, {"message", "Invalid camera index"}};
				res.status = BAD_REQUEST;
				res.set_content(jsonResponse.dump(), "application/json");
				return;
			}

			auto camera = mCameras[cameraIndex];
			auto defaultFunctionMode = ins_camera::CameraFunctionMode::FUNCTION_MODE_NORMAL_IMAGE;
			auto functionMode = req.has_param("functionMode")
									? static_cast<ins_camera::CameraFunctionMode>(std::stoi(req.get_param_value("functionMode")))
									: defaultFunctionMode;

			auto defaultCaptureSettings = camera->getCaptureSettings(functionMode);

			int defaultContrast = defaultCaptureSettings
									  ? defaultCaptureSettings->GetIntValue(ins_camera::CaptureSettings::CaptureSettings_Contrast)
									  : 64;
			int defaultSaturation = defaultCaptureSettings
										? defaultCaptureSettings->GetIntValue(ins_camera::CaptureSettings::CaptureSettings_Saturation)
										: 64;
			int defaultBrightness = defaultCaptureSettings
										? defaultCaptureSettings->GetIntValue(ins_camera::CaptureSettings::CaptureSettings_Brightness)
										: 0;
			int defaultSharpness = defaultCaptureSettings
									   ? defaultCaptureSettings->GetIntValue(ins_camera::CaptureSettings::CaptureSettings_Sharpness)
									   : 3;
			auto defaultWBValue = defaultCaptureSettings ? defaultCaptureSettings->WhiteBalance()
														 : ins_camera::PhotographyOptions_WhiteBalance_WB_AUTO;

			int contrast = req.has_param("contrast") ? std::stoi(req.get_param_value("contrast")) : defaultContrast;
			int saturation = req.has_param("saturation") ? std::stoi(req.get_param_value("saturation")) : defaultSaturation;
			int brightness = req.has_param("brightness") ? std::stoi(req.get_param_value("brightness")) : defaultBrightness;
			int sharpness = req.has_param("sharpness") ? std::stoi(req.get_param_value("sharpness")) : defaultSharpness;
			auto wbValue =
				req.has_param("wbValue")
					? static_cast<ins_camera::PhotographyOptions_WhiteBalance>(std::stoi(req.get_param_value("wbValue")))
					: defaultWBValue;
			try
			{
				camera->setCaptureSettings(contrast, saturation, brightness, sharpness, wbValue, functionMode);
				jsonResponse = {{"status", "success"}, {"message", "Capture settings updated successfully"}};
				res.status = OK;  // OK
			}
			catch (const std::exception& e)
			{
				jsonResponse = {{"status", "error"}, {"message", "Failed to set capture settings", "errorDetail", e.what()}};
				res.status = INTERNAL_SERVER_ERROR;	 // Internal Server Error
			}

			res.set_content(jsonResponse.dump(), "application/json");
		}
	);
}
