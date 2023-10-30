/*
 * Shared State
 *
 * Copyright (c) 2023  Gioacchino Mazzurco <gio@eigenlab.org>
 * Copyright (c) 2023  Javier Jorge <jjorge@inti.gob.ar>
 * Copyright (c) 2023  Instituto Nacional de Tecnología Industrial
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

#include <iostream>
#include <unistd.h>

#include "io_context.hh"
#include "file_read_operation.hh"
#include "async_file_desc.hh"

#include <util/rsdebuglevel2.h>


ReadOp::ReadOp(
        std::shared_ptr<AsyncFileDescriptor> afd,
        uint8_t* buffer, std::size_t len,
        std::error_condition* ec ):
    BlockSyscall<ReadOp, ssize_t>(ec),
    mAFD{afd}, mBuffer{buffer}, mLen{len}
{
	mAFD->getIOContext().watchRead(mAFD.get());
}

ReadOp::~ReadOp()
{
	RS_DBG2(*mAFD);
	mAFD->getIOContext().unwatchRead(mAFD.get());
}

ssize_t ReadOp::syscall()
{
	ssize_t bytesread = read(mAFD->getFD(), mBuffer, mLen);

    /* this method is invoked at least once but the pipe is not free.
     * this is not problem since the BlockSyscall::await_suspend will test for -1 return value and test errno (EWOULDBLOCK or EAGAIN)
     * and then suspend the execution until a new notification arrives
     */
    if (bytesread == -1)
    {
		RS_WARN(*mAFD, " ", rs_errno_to_condition(errno));
    }
	RS_DBG0(*mAFD, " read ", bytesread , " bytes" );
    return bytesread;
}

void ReadOp::suspend()
{
	// mAFD->coroRecv_ = mAwaitingCoroutine;
	mAFD->addPendingOp(mAwaitingCoroutine);
}
