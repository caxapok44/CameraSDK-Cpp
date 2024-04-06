
#include <httplib.h>

#include <cstddef>
#include <json.hpp>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "../LeticoCamera/leticoCamera.h"
#include "../utils.hpp"

using json = nlohmann::json;
namespace Letico
{

	class LeticoHttpServer
	{
	public:
		enum ResponseStatus
		{
			OK = 200,
			BAD_REQUEST = 400,
			NOT_FOUND = 404,
			INTERNAL_SERVER_ERROR =500,
		};
		enum RequestType
		{
			GET,
			POST
		};

		LeticoHttpServer(std::vector<std::shared_ptr<LeticoCamera>> cameras);
		~LeticoHttpServer();

	private:
		void createEndpoints();
		void createServer();

		std::shared_ptr<httplib::Server> mServer;
		std::vector<std::shared_ptr<LeticoCamera>> mCameras;
		std::thread mServerThread;

		std::string mServerAddress;
		int mPort;
		std::string mNginxMediaUrl;
	};
}