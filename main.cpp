#include <iostream>
#include "torrent/TorSyncManager.h"

int main()
{
	std::shared_ptr<TorSyncManager> tsm = std::make_shared<TorSyncManager>();
	
	auto handle = tsm->Fetch("2d6c88d4ce9e69f43c2798cc571fba7a02328a65", L"C:\\dest");
	handle.WaitUntilFinished();
	
	return 0;
}