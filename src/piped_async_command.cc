/*
 * Shared State
 *
 * Copyright (C) 2023  Gioacchino Mazzurco <gio@eigenlab.org>
 * Copyright (C) 2023  Javier Jorge <jjorge@inti.gob.ar>
 * Copyright (C) 2023  Instituto Nacional de Tecnología Industrial
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

#include <cstdio>
#include <fcntl.h>
#include <sys/types.h>
#include <iostream>
#include <unistd.h>
#include <vector>
#include <signal.h>
#include <sys/types.h>
#include <iterator>
#include <sstream>

#include "piped_async_command.hh"
#include "io_context.hh"

#include <util/rsdebug.h>
#include <util/rsdebuglevel2.h>

#ifndef __NR_pidfd_open
#define __NR_pidfd_open 434 /* System call # on most architectures */
#endif

static int pidfd_open(pid_t pid, unsigned int flags)
{
    return syscall(__NR_pidfd_open, pid, flags);
}

/*static*/ std::shared_ptr<PipedAsyncCommand> PipedAsyncCommand::execute(
        std::string cmd, IOContext& ioContext,
        std::error_condition* errbub )
{
	int parentToChildPipe[2]; // mFd_w
	int childToParentPipe[2]; // mFd_r
	auto& PARENT_READ  = childToParentPipe[0];
	auto& CHILD_WRITE  = childToParentPipe[1];
	auto& CHILD_READ   = parentToChildPipe[0];
	auto& PARENT_WRITE = parentToChildPipe[1];

	if (pipe(parentToChildPipe) == -1)
	{
		rs_error_bubble_or_exit(
		            rs_errno_to_condition(errno), errbub,
		            "pipe(mFd_w) failed" );
		return nullptr;
	}

	if (pipe(childToParentPipe) == -1)
	{
		// Close the previously open pipe
		close(parentToChildPipe[1]);
		close(parentToChildPipe[0]);

		rs_error_bubble_or_exit(
		            rs_errno_to_condition(errno), errbub,
		            "pipe(mFd_r) failed" );
		return nullptr;
	}

	pid_t forkRetVal = fork();
	if (forkRetVal == -1)
	{
		auto forkErrno = errno;

		// Close the previously opened pipes
		close(CHILD_READ);
		close(CHILD_WRITE);
		close(PARENT_READ);
		close(PARENT_WRITE);

		rs_error_bubble_or_exit(
		            rs_errno_to_condition(forkErrno), errbub,
		            "fork() failed" );
		return nullptr;
	}

	if( forkRetVal > 0)
	{
		int childWaitFD = pidfd_open(forkRetVal, 0);
		if(childWaitFD == -1)
		{
			auto pidfdOpenErrno = errno;

			// Close the previously opened pipes
			close(CHILD_READ);
			close(CHILD_WRITE);
			close(PARENT_READ);
			close(PARENT_WRITE);

			// Kill child process
			kill(forkRetVal, SIGKILL);

			rs_error_bubble_or_exit(
			            rs_errno_to_condition(pidfdOpenErrno), errbub,
			            "pidfd_open(...) failed" );
			return nullptr;
		}

		/* At this point graceful error handling becomes tricky, but I bet none
		 * of this functions should fail under non-dramatically pathological
		 * conditions so let's see what happens. If failure here still happens
		 * I am up to reading bug reports, and curious on how to reproduce that
		 * situation */

		auto tPac = ioContext.registerFD<PipedAsyncCommand>(childWaitFD);
		ioContext.attach(tPac.get());

		tPac->mChildProcessId = forkRetVal;

		tPac->mReadEnd = ioContext.registerFD(PARENT_READ);
		ioContext.attachReadonly(tPac->mReadEnd.get());

		tPac->mWriteEnd = ioContext.registerFD(PARENT_WRITE);
		ioContext.attachWriteOnly(tPac->mWriteEnd.get());

		// Close child ends of the pipes
		close(CHILD_READ); close(CHILD_WRITE);

		return tPac;
	}

/* BEGIN: CODE EXECUTED ON THE CHILD PROCESS **********************************/
	if (forkRetVal == 0)
	{
		/* No need to keep those FD opened on the child */
		close(PARENT_WRITE);
		close(PARENT_READ);

		// Map child side of the pipe to child process standard input and output
		dup2(CHILD_READ,  STDIN_FILENO); close(CHILD_READ);
		dup2(CHILD_WRITE, STDOUT_FILENO); close(CHILD_WRITE);

		std::stringstream ss(cmd);
		std::istream_iterator<std::string> begin(ss);
		std::istream_iterator<std::string> end;
		std::vector<std::string> vstrings(begin, end);
		std::copy( vstrings.begin(), vstrings.end(),
		           std::ostream_iterator<std::string>(std::cout, "\n") );

		std::vector<char *> argcexec(vstrings.size(), nullptr);
		for (int i = 0; i < vstrings.size(); i++)
		{
			argcexec[i] = vstrings[i].data();
		}
		argcexec.push_back(nullptr); // NULL terminate the command line

		/* The first argument to execvp should be the same as the first element
		 * in argc */
		if(execvp(argcexec.data()[0], argcexec.data()) == -1)
		{
			/* We are on the child process so no-one must be there to deal with
			 * the error condition bubbling up, pass nullptr so it just
			 * terminate printing error details */
			std::error_condition* nully = nullptr;
			rs_error_bubble_or_exit(
			            rs_errno_to_condition(errno), errbub,
			            "execvp(...) failed" );
		}
	}
/* END: CODE EXECUTED ON THE CHILD PROCESS ************************************/

	// No one should get here, just make the code analizer happy
	return nullptr;
}

ReadOp PipedAsyncCommand::readStdOut(
        uint8_t* buffer, std::size_t len, std::error_condition* errbub)
{
	return ReadOp{mReadEnd, buffer, len};
}

WriteOp PipedAsyncCommand::writeStdIn(
        const uint8_t* buffer, std::size_t len, std::error_condition* errbub )
{
	RS_DBG2(*mWriteEnd, " buffer: ", buffer, " len: ", len);
	return WriteOp{*mWriteEnd, buffer, len, errbub};
}

std::task<bool>  PipedAsyncCommand::finishwriting(std::error_condition* errbub)
{
	auto mRet = co_await mIOContext.closeAFD(mWriteEnd, errbub);
	if(mRet) mWriteEnd.reset();
	co_return mRet;
}

/**
 * When the filedescriptor is closed but it notifies with a read event the 
 * file descriptor is marked as done, to be able to stop reading
*/
bool PipedAsyncCommand::doneReading()
{
	return mReadEnd.get()->doneRecv_;
}

std::task<bool> PipedAsyncCommand::finishReading(std::error_condition* errbub)
{
	auto mRet = co_await mIOContext.closeAFD(mReadEnd, errbub);
	if(mRet) mReadEnd.reset();
	co_return mRet;
}

/**
 * Asynchronously waits for a process to die.
 * @warning if this method is not called the forked process will be
 * a zombi.
 */
/*static*/ std::task<pid_t> PipedAsyncCommand::waitForProcessTermination(
        std::shared_ptr<PipedAsyncCommand> pac,
        std::error_condition* errbub )
{
	auto dPid = co_await DyingProcessWaitOperation(*pac, pac->getFD());
	co_await pac->getIOContext().closeAFD(
	            std::static_pointer_cast<AsyncFileDescriptor>(pac), errbub );
	co_return dPid;
}
