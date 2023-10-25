/*
 * Shared State
 *
 * Copyright (C) 2023  Gioacchino Mazzurco <gio@eigenlab.org>
 * Copyright (C) 2023  Asociación Civil Altermundi <info@altermundi.net>
 *
 * This program is free software: you can redistribute it and/or modify it under
 * the terms of the GNU Affero General Public License as published by the
 * Free Software Foundation, version 3.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.
 * See the GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program. If not, see <https://www.gnu.org/licenses/>
 *
 * SPDX-License-Identifier: AGPL-3.0-only
 */

#include "io_context.hh"
#include "socket.hh"
#include "file_read_operation.hh"
#include "sharedstate.hh"

#include <cstdio>
#include <cstdlib>
#include <arpa/inet.h>

#include <util/rsdebug.h>
#include <util/rsdebuglevel2.h>
#include <util/rsnet.h>
#include <util/stacktrace.h>

static CrashStackTrace gCrashStackTrace;

std::task<> sendStdInput(
        std::string dataTypeName, std::string peerAddrStr,
        IOContext& ioContext )
{
	SharedState::NetworkMessage netMessage;

#ifdef GIO_DUMMY_TEST
	netMessage.mTypeName = dataTypeName;
	std::string caccaData = "cacapisciapuzza";
	netMessage.mData.assign(caccaData.begin(), caccaData.end());
#else
	auto flags = fcntl(STDIN_FILENO, F_GETFL, 0);
	fcntl(STDIN_FILENO, F_SETFL, flags | O_NONBLOCK);

	auto aStdIn = std::make_shared<AsyncFileDescriptor>(
	            STDIN_FILENO, ioContext );

	netMessage.mData.clear();
	netMessage.mData.resize(SharedState::DATA_MAX_LENGHT);


	bool finish = false;
	std::size_t totalRead = 0;
	while(!finish)
	{
		totalRead += co_await ReadOp(
		            aStdIn,
		            reinterpret_cast<uint8_t*>(netMessage.mData.data()),
		            netMessage.mData.size() - totalRead );
		finish = true;
	}
	fcntl(STDIN_FILENO, F_SETFL, flags);
	netMessage.mData.resize(totalRead);

	RS_DBG4( "netMessage.mTypeName: ", netMessage.mTypeName,
	         " netMessage.mData:\n", netMessage.mData );
#endif

	sockaddr_storage peerAddr;
	sockaddr_storage_inet_pton(peerAddr, peerAddrStr);
	sockaddr_storage_setport(peerAddr, 3490);

	auto socket = co_await ConnectingSocket::connect(peerAddr, ioContext);
	auto sentMessageSize = netMessage.mData.size();
	auto totalSent = co_await
	        SharedState::sendNetworkMessage(*socket.get(), netMessage);
	auto totalReceived = co_await
	        SharedState::receiveNetworkMessage(*socket.get(), netMessage);

	RS_DBG2( "Sent message type: ", dataTypeName,
	         " Sent message size: ", sentMessageSize,
	         " Received message type: ", netMessage.mTypeName,
	         " Received message size: ", netMessage.mTypeName.size(),
	         " Total sent bytes: ", totalSent,
	         " Total received bytes: ", totalReceived );

	std::cout << std::string(netMessage.mData.begin(), netMessage.mData.end())
	          << std::endl;

	exit(0);
}

int main(int argc, char* argv[])
{
	if(argc < 3)
	{
		RS_FATAL("Need type name and peer IP address");
		return -EINVAL;
	}

	auto ioContext = IOContext::setup();

	std::string dataTypeName(argv[1]);
	std::string peerAddrStr(argv[2]);

	RS_INFO("Got dataTypeName: ", dataTypeName, " peerAddrStr: ", peerAddrStr);

	auto sendTask = sendStdInput(dataTypeName, peerAddrStr, *ioContext.get());
	sendTask.resume();

	ioContext->run();
	return 0;
}
