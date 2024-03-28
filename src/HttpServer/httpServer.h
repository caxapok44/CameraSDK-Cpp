
#include <httplib.h>
#include <cstddef>
#include <json.hpp>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "../LeticoCamera/leticoCamera.h"

using json = nlohmann::json;
namespace Letico
{


	class LeticoHttpServer
	{
	public:
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
	};
}