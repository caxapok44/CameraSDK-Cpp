#pragma once
#include "httpServer.h"

#include <cstddef>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

#include "camera/photography_settings.h"

using namespace Letico;

LeticoHttpServer::LeticoHttpServer(std::vector<std::shared_ptr<LeticoCamera>> cameras): mCameras(std::move(cameras))
{
	try
	{
		YAML::Node config = YAML::LoadFile("config/service.yaml");

		if (config["server"])
		{
			mServerAddress = config["server"]["address"].as<std::string>("localhost");
			mPort = config["server"]["port"].as<int>(9091);
			mNginxMediaUrl = config["nginx"]["mediaUrl"].as<std::string>("localhost");
		}
		else
		{
			std::cerr << "Server configuration not found in config.yaml. Using default values." << std::endl;
			mServerAddress = "localhost";
			mPort = 9091;
			mNginxMediaUrl = "localhost/media/";
		}
	}
	catch (const YAML::Exception& e)
	{
		std::cerr << "Error reading config.yaml: " << e.what() << std::endl;
		mServerAddress = "localhost";
		mPort = 9091;
		mNginxMediaUrl = "localhost/media/";
	}

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
		"/api/v1/healthy",
		[&](const httplib::Request&, httplib::Response& res)
		{
			nlohmann::json jsonResponse;
			jsonResponse = {{"status", "healthy"}};

			res.set_content(jsonResponse.dump(), "application/json");
		}
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
			nlohmann::json jsonResponse;

			auto req_json = nlohmann::json::parse(req.body);
			auto fileName = req_json.value("fileName", "");

			std::ifstream fileStream(fileName, std::ios::binary | std::ios::ate);
			if (!fileStream.is_open())
			{
				res.status = NOT_FOUND;
				jsonResponse = {{"status", "error"}, {"message", "File not found"}};
				res.set_content(jsonResponse.dump(), "application/json");
				return;
				return;
			}

			auto fileSize = fileStream.tellg();
			fileStream.seekg(0, std::ios::beg);

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
			nlohmann::json jsonResponse;

			// Parse request body into JSON
			auto req_json = nlohmann::json::parse(req.body);

			size_t cameraIndex = req_json.value("cameraIndex", 0);	// Default to 0 if not provided
			if (cameraIndex >= mCameras.size())
			{
				jsonResponse = {{"status", "error"}, {"message", "Invalid camera index"}};
				res.status = 400;  // Bad Request
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
			auto fileName = std::filesystem::path(folderUrl).filename();
			std::string nginxUrl = mNginxMediaUrl + fileName.string();
			jsonResponse = {
				{"status", "success"},
				{"message", "Photo taken successfully"},
				{"deviceUrl", deviceUrl},
				{"homeUrl", folderUrl},
				{"wwwUrl", wwwUrl},
				{"nginxUrl", nginxUrl}};

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
				// Parse request body into JSON
				auto req_json = nlohmann::json::parse(req.body);

				size_t cameraIndex = req_json.value("cameraIndex", 0);	// Default to 0 if not provided
				if (cameraIndex >= mCameras.size())
				{
					jsonResponse = {{"status", "error"}, {"message", "Invalid camera index"}};
					res.status = 400;  // Bad Request
					res.set_content(jsonResponse.dump(), "application/json");
					return;
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
				// Parse request body into JSON
				auto req_json = nlohmann::json::parse(req.body);

				size_t cameraIndex = req_json.value("cameraIndex", 0);	// Default to 0 if not provided
				if (cameraIndex >= mCameras.size())
				{
					jsonResponse = {{"status", "error"}, {"message", "Invalid camera index"}};
					res.status = 400;  // Bad Request
					res.set_content(jsonResponse.dump(), "application/json");
					return;
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
			nlohmann::json jsonResponse;

			// Parse request body into JSON
			auto req_json = nlohmann::json::parse(req.body);

			size_t cameraIndex = req_json.value("cameraIndex", 0);	// Default to 0 if not provided
			if (cameraIndex >= mCameras.size())
			{
				jsonResponse = {{"status", "error"}, {"message", "Invalid camera index"}};
				res.status = 400;  // Bad Request
				res.set_content(jsonResponse.dump(), "application/json");
				return;
			}

			// Default values for optional parameters
			int bitrate = req_json.value("bitrate", 1024 * 1024 * 10);
			auto resolution = static_cast<ins_camera::VideoResolution>(
				req_json.value("resolution", static_cast<int>(ins_camera::VideoResolution::RES_3040_3040P24))
			);

			auto functionMode = static_cast<ins_camera::CameraFunctionMode>(
				req_json.value("functionMode", static_cast<int>(ins_camera::CameraFunctionMode::FUNCTION_MODE_NORMAL_VIDEO))
			);

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
			nlohmann::json jsonResponse;

			// Parse request body into JSON
			auto req_json = nlohmann::json::parse(req.body);

			size_t cameraIndex = req_json.value("cameraIndex", 0);	// Default to 0 if not provided
			if (cameraIndex >= mCameras.size())
			{
				jsonResponse = {{"status", "error"}, {"message", "Invalid camera index"}};
				res.status = 400;  // Bad Request
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
			for (const auto& folderUrl : folderUrls)
			{
				auto fileName = std::filesystem::path(folderUrl).filename();
				wwwUrls.push_back(mNginxMediaUrl + fileName.string());
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
			nlohmann::json jsonResponse;

			// Parse request body into JSON
			auto req_json = nlohmann::json::parse(req.body);

			size_t cameraIndex = req_json.value("cameraIndex", 0);	// Default to 0 if not provided
			if (cameraIndex >= mCameras.size())
			{
				jsonResponse = {{"status", "error"}, {"message", "Invalid camera index"}};
				res.status = 400;  // Bad Request
				res.set_content(jsonResponse.dump(), "application/json");
				return;
			}

			std::string fileToDelete = req_json.value("fileToDelete", "");
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
			nlohmann::json jsonResponse;

			// Parse request body into JSON
			auto req_json = nlohmann::json::parse(req.body);

			size_t cameraIndex = req_json.value("cameraIndex", 0);	// Default to 0 if not provided
			if (cameraIndex >= mCameras.size())
			{
				jsonResponse = {{"status", "error"}, {"message", "Invalid camera index"}};
				res.status = 400;  // Bad Request
				res.set_content(jsonResponse.dump(), "application/json");
				return;
			}

			std::string fileToDownload = req_json.value("fileToDownload", "");
			auto folderUrl = mCameras[cameraIndex]->downloadFile(fileToDownload);
			jsonResponse = {
				{"status", "success"}, {"message", "File downloaded successfully"}, {"deviceUrl", fileToDownload}, {"folderUrl", folderUrl}};

			res.set_content(jsonResponse.dump(), "application/json");
		}
	);
	//======== Start live stream ======== // Stream delegate should be set
	mServer->Post(
		"/api/v1/startLiveStream",
		[&](const httplib::Request& req, httplib::Response& res)
		{
			nlohmann::json jsonResponse;

			// Parse request body into JSON
			auto req_json = nlohmann::json::parse(req.body);

			size_t cameraIndex = req_json.value("cameraIndex", 0);	// Default to 0 if not provided
			if (cameraIndex >= mCameras.size())
			{
				jsonResponse = {{"status", "error"}, {"message", "Invalid camera index"}};
				res.status = 400;  // Bad Request
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
			nlohmann::json jsonResponse;

			// Parse request body into JSON
			auto req_json = nlohmann::json::parse(req.body);

			size_t cameraIndex = req_json.value("cameraIndex", 0);	// Default to 0 if not provided
			if (cameraIndex >= mCameras.size())
			{
				jsonResponse = {{"status", "error"}, {"message", "Invalid camera index"}};
				res.status = 400;  // Bad Request
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
			nlohmann::json jsonResponse;

			// Parse request body into JSON
			auto req_json = nlohmann::json::parse(req.body);

			size_t cameraIndex = req_json.value("cameraIndex", 0);	// Default to 0 if not provided
			if (cameraIndex >= mCameras.size())
			{
				jsonResponse = {{"status", "error"}, {"message", "Invalid camera index"}};
				res.status = 400;  // Bad Request
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
			nlohmann::json jsonResponse;

			// Parse request body into JSON
			auto req_json = nlohmann::json::parse(req.body);

			size_t cameraIndex = req_json.value("cameraIndex", 0);	// Default to 0 if not provided
			if (cameraIndex >= mCameras.size())
			{
				jsonResponse = {{"status", "error"}, {"message", "Invalid camera index"}};
				res.status = 400;  // Bad Request
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
			nlohmann::json jsonResponse;

			// Parse request body into JSON
			auto req_json = nlohmann::json::parse(req.body);

			size_t cameraIndex = req_json.value("cameraIndex", 0);	// Default to 0 if not provided
			if (cameraIndex >= mCameras.size())
			{
				jsonResponse = {{"status", "error"}, {"message", "Invalid camera index"}};
				res.status = 400;  // Bad Request
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
			nlohmann::json jsonResponse;

			// Parse request body into JSON
			auto req_json = nlohmann::json::parse(req.body);

			size_t cameraIndex = req_json.value("cameraIndex", 0);	// Default to 0 if not provided
			if (cameraIndex >= mCameras.size())
			{
				jsonResponse = {{"status", "error"}, {"message", "Invalid camera index"}};
				res.status = 400;  // Bad Request
				res.set_content(jsonResponse.dump(), "application/json");
				return;
			}

			auto camera = mCameras[cameraIndex];

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
			nlohmann::json jsonResponse;
			try
			{
				// Parse request body into JSON
				auto req_json = nlohmann::json::parse(req.body);

				size_t cameraIndex = req_json.value("cameraIndex", 0);	// Default to 0 if not provided
				if (cameraIndex >= mCameras.size())
				{
					jsonResponse = {{"status", "error"}, {"message", "Invalid camera index"}};
					res.status = 400;  // Bad Request
					res.set_content(jsonResponse.dump(), "application/json");
					return;
				}

				auto camera = mCameras[cameraIndex];
				auto defaultFunctionMode = ins_camera::CameraFunctionMode::FUNCTION_MODE_NORMAL_IMAGE;
				auto functionMode =
					static_cast<ins_camera::CameraFunctionMode>(req_json.value("functionMode", static_cast<int>(defaultFunctionMode)));

				auto defaultExposureSettings = camera->getExposureSettings(functionMode);

				int defaultBias = defaultExposureSettings ? defaultExposureSettings->EVBias() : 0;
				double defaultShutterSpeed = defaultExposureSettings ? defaultExposureSettings->ShutterSpeed() : 1.0 / 120.0;
				int defaultIso = defaultExposureSettings ? defaultExposureSettings->Iso() : 800;
				auto defaultExposureMode =
					defaultExposureSettings
						? defaultExposureSettings->ExposureMode()
						: ins_camera::PhotographyOptions_ExposureMode::PhotographyOptions_ExposureOptions_Program_AUTO;

				int bias = req_json.value("bias", defaultBias);
				double shutterSpeed = req_json.value("shutterSpeed", defaultShutterSpeed);
				int iso = req_json.value("iso", defaultIso);
				auto exposureMode = static_cast<ins_camera::PhotographyOptions_ExposureMode>(
					req_json.value("exposureMode", static_cast<int>(defaultExposureMode))
				);

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
			nlohmann::json jsonResponse;
			try
			{
				// Parse request body into JSON
				auto req_json = nlohmann::json::parse(req.body);

				size_t cameraIndex = req_json.value("cameraIndex", 0);	// Default to 0 if not provided
				if (cameraIndex >= mCameras.size())
				{
					jsonResponse = {{"status", "error"}, {"message", "Invalid camera index"}};
					res.status = 400;  // Bad Request
					res.set_content(jsonResponse.dump(), "application/json");
					return;
				}

				auto camera = mCameras[cameraIndex];
				auto defaultFunctionMode = ins_camera::CameraFunctionMode::FUNCTION_MODE_NORMAL_IMAGE;
				auto functionMode =
					static_cast<ins_camera::CameraFunctionMode>(req_json.value("functionMode", static_cast<int>(defaultFunctionMode)));

				auto defaultCaptureSettings = camera->getCaptureSettings(functionMode);

				int defaultContrast = defaultCaptureSettings
										  ? defaultCaptureSettings->GetIntValue(ins_camera::CaptureSettings::CaptureSettings_Contrast)
										  : 64;
				int defaultSaturation =
					defaultCaptureSettings
						? defaultCaptureSettings->GetIntValue(ins_camera::CaptureSettings::CaptureSettings_Saturation)
						: 64;
				int defaultBrightness =
					defaultCaptureSettings
						? defaultCaptureSettings->GetIntValue(ins_camera::CaptureSettings::CaptureSettings_Brightness)
						: 0;
				int defaultSharpness = defaultCaptureSettings
										   ? defaultCaptureSettings->GetIntValue(ins_camera::CaptureSettings::CaptureSettings_Sharpness)
										   : 3;
				auto defaultWBValue = defaultCaptureSettings ? defaultCaptureSettings->WhiteBalance()
															 : ins_camera::PhotographyOptions_WhiteBalance_WB_AUTO;

				int contrast = req_json.value("contrast", defaultContrast);
				int saturation = req_json.value("saturation", defaultSaturation);
				int brightness = req_json.value("brightness", defaultBrightness);
				int sharpness = req_json.value("sharpness", defaultSharpness);
				auto wbValue =
					static_cast<ins_camera::PhotographyOptions_WhiteBalance>(req_json.value("wbValue", static_cast<int>(defaultWBValue)));

				std::cout << "1) Try to set for for " << functionMode << std::endl;
				std::cout << "Contrast: " << contrast << std::endl;
				std::cout << "Saturation: " << saturation << std::endl;
				std::cout << "Brightness: " << brightness << std::endl;
				std::cout << "Sharpness: " << sharpness << std::endl;
				std::cout << "White and Black balance: " << wbValue << std::endl;

				// Perform the operation
				auto errorCode = camera->setCaptureSettings(contrast, saturation, brightness, sharpness, wbValue, functionMode);
				if (!errorCode.empty())
				{
					throw std::runtime_error(errorCode);
				}

				jsonResponse = {{"status", "success"}, {"message", "Capture settings updated successfully"}};
				res.status = OK;  // OK
				res.set_content(jsonResponse.dump(), "application/json");
				return;
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
