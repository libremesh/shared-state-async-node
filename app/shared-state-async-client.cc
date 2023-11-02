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
#include <signal.h>

#include <util/rsdebug.h>
#include <util/rsdebuglevel2.h>
#include <util/rsnet.h>
#include <util/stacktrace.h>

static CrashStackTrace gCrashStackTrace;

std::task<> sendStdInput(
        std::string dataTypeName, const sockaddr_storage& peerAddr,
        IOContext& ioContext )
{
	SharedState::NetworkMessage netMessage;
	netMessage.mTypeName = dataTypeName;

#ifdef GIO_DUMMY_TEST
	std::string caccaData = "cacapisciapuzza";
	netMessage.mData.assign(caccaData.begin(), caccaData.end());
#else
	auto aStdIn = ioContext.registerFD(STDIN_FILENO);

	netMessage.mData.clear();
	netMessage.mData.resize(SharedState::DATA_MAX_LENGHT);

	std::size_t totalRead = 0;
	ssize_t readBytes = 0;
	do
	{
		 auto readBytes = co_await ReadOp(
		             aStdIn, netMessage.mData.data(),
		            netMessage.mData.size() - totalRead );
		totalRead += readBytes;
		RS_DBG0( "Got from STDIN readBytes: ", readBytes,
		         " totalRead: ", totalRead,
		         " data:", hexDump(netMessage.mData.data(), totalRead) );
	}
	while(readBytes && totalRead < SharedState::DATA_MAX_LENGHT);

	netMessage.mData.resize(totalRead);

	co_await ioContext.closeAFD(aStdIn);

	RS_DBG4( "netMessage.mTypeName: ", netMessage.mTypeName,
	         " netMessage.mData:\n", netMessage.mData );
#endif

	auto socket = co_await ConnectingSocket::connect(peerAddr, ioContext);
	auto sentMessageSize = netMessage.mData.size();
	auto totalSent = co_await
	        SharedState::sendNetworkMessage(*socket, netMessage);
	auto totalReceived = co_await
	        SharedState::receiveNetworkMessage(*socket, netMessage);

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

	std::string dataTypeName(argv[1]);
	std::string peerAddrStr(argv[2]);

	sockaddr_storage peerAddr;
	if(!sockaddr_storage_inet_pton(peerAddr, peerAddrStr))
	{
		RS_FATAL("Invalid IP address");
		return -static_cast<int>(std::errc::bad_address);
	}
	sockaddr_storage_setport(peerAddr, 3490);

	RS_INFO("Got dataTypeName: ", dataTypeName, " peerAddrStr: ", peerAddrStr);

	/* We expect write failures, expecially on sockets, to occur but we want to
	 * handle them where the error occurs rather than in a SIGPIPE handler */
	signal(SIGPIPE, SIG_IGN);

	auto ioContext = IOContext::setup();
	auto sendTask = sendStdInput(dataTypeName, peerAddr, *ioContext.get());
	sendTask.resume();
	ioContext->run();

	return 0;
}
