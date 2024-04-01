#include <memory>
#include "HttpServer/httpServer.h"
#include "LeticoCamera/leticoCamera.h"
#include "Menu/menu.h"


int main()
{
	std::shared_ptr<LeticoCamera> camera = std::make_shared<LeticoCamera>();

	Letico::LeticoHttpServer http({camera});

	Menu menu({camera});
	menu.showOptions();

	return 0;
}
