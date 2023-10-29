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

#pragma once

#include <cerrno>
#include <coroutine>
#include <memory>
#include <type_traits>
#include <iostream>

#include <util/rsdebuglevel2.h>
#include <util/stacktrace.h>


/**
 * @brief BlockSyscall is a base class for all kind of asynchronous syscalls
 * @tparam SyscallOpt child class, passed as template paramether to avoid
 *	dynamic dispatch performance hit
 * @tparam ReturnValue type of the return value of the real syscall
 * The costructor take a pointer to a std::error_condition to deal with errors,
 * if nullptr is passed it is assumed that the upstream caller won't deal with
 * the error so the program will exit after printing where the error happened.
 * On the contrary if a valid pointer is passed, when an error occurr the
 * information about the error will be stored there for the upstream caller to
 * deal with it.
 *
 * Derived classes MUST implement the following methods
 * @code{.cpp}
 * ReturnValue syscall();
 * void suspend();
 * @endcode
 *
 * The syscall method is where the actuall syscall must happen.
 *
 * TODO: What is the suspend method supposed to do?
 *
 * Pure virtual definition not included at moment to virtual method tables
 * generation (haven't verified if this is necessary or not).
 */
template <typename SyscallOpt, typename ReturnValue>
class BlockSyscall // Awaiter
{
public:
	BlockSyscall(std::error_condition* ec):
	    mHaveSuspend{false}, mError{ec} {}

	bool await_ready() const noexcept
	{
		RS_DBG3("");
		return false;
	}

	bool await_suspend(std::coroutine_handle<> awaitingCoroutine)
	{
		RS_DBG3("");

		static_assert(std::is_base_of_v<BlockSyscall, SyscallOpt>);
		mAwaitingCoroutine = awaitingCoroutine;
		mReturnValue = static_cast<SyscallOpt *>(this)->syscall();
		mHaveSuspend =
		    mReturnValue == -1 && (
		            errno == EAGAIN ||
		            errno == EWOULDBLOCK ||
		            errno == EINPROGRESS );
		if (mHaveSuspend)
		{
			/* The syscall indicated we must wait, and retry later so let's
			 * suspend to return the control to the caller and be resumed later
			 */
			RS_DBG2( "let suspend for now mReturnValue: ", mReturnValue,
			         " && errno: ", rs_errno_to_condition(errno) );
			static_cast<SyscallOpt *>(this)->suspend();
		}
		else if (mReturnValue == -1)
		{
			/* The syscall failed for other reason let's notify the caller if
			 * possible or close the program printing an error */
			rs_error_bubble_or_exit(
			            rs_errno_to_condition(errno), mError,
			            " syscall failed" );
		}
		// We can keep going, no need to do suspend, on failure
		return mHaveSuspend;
	}

	ReturnValue await_resume()
	{
		RS_DBG3("");

		if(mHaveSuspend)
		{
			// We had to suspend last time, so we need to call the syscall again
			mReturnValue = static_cast<SyscallOpt *>(this)->syscall();
		}

		return mReturnValue;
	}

protected:
	bool mHaveSuspend;
	std::coroutine_handle<> mAwaitingCoroutine;

	std::error_condition* mError;
	ReturnValue mReturnValue;
};
