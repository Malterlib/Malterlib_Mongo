// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <Mib/Test/Test>
#include <Mib/Mongo/Client>
#include <Mib/Concurrency/AsyncDestroy>

// Exercise the driver cancellation primitive without requiring a database.
#define MONGOC_INSIDE
#include <mongoc/mongoc-socket-private.h>
#include <mongoc/mongoc-interrupt-private.h>
#undef MONGOC_INSIDE

namespace
{
	using namespace NMib;
	using namespace NMib::NConcurrency;

	struct CSocketInterrupt_Tests : NTest::CTest
	{
		void f_DoTests()
		{
			DMibTestSuite("Cancellation") -> TCFuture<void>
			{
				TCActor<NMongo::CMongoClientActor> Runtime{fg_Construct(NMongo::CMongoConnectionSettings(), NStr::CStr("test")), "Mongo socket tests"};
				auto DestroyRuntime = co_await fg_AsyncDestroy(Runtime);
				co_await fg_Dispatch
					(
						Runtime
						, []
						{
							for (umint Case = 0; Case < 4; ++Case)
							{
								DMibTestPath(Case == 0 ? "Receive" : Case == 1 ? "Poll" : Case == 2 ? "Receive after idle" : "Already cancelled");
								auto *pInterrupt = _mongoc_interrupt_new(5000);
								DMibAssertTrue(pInterrupt != nullptr);
								auto DestroyInterrupt = g_OnScopeExit / [&]
									{
										_mongoc_interrupt_destroy(pInterrupt);
									}
								;
								auto *pListener = mongoc_socket_new(AF_INET, SOCK_STREAM, 0, nullptr);
								auto *pReader = mongoc_socket_new(AF_INET, SOCK_STREAM, 0, pInterrupt);
								mongoc_socket_t *pWriter = nullptr;
								auto DestroySockets = g_OnScopeExit / [&]
									{
										mongoc_socket_destroy(pWriter);
										mongoc_socket_destroy(pReader);
										mongoc_socket_destroy(pListener);
									}
								;
								DMibAssertTrue(pListener != nullptr && pReader != nullptr);
								sockaddr_in Address{};
								Address.sin_family = AF_INET;
								Address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
								DMibAssert(mongoc_socket_bind(pListener, (sockaddr *)&Address, sizeof(Address)), ==, 0);
								DMibAssert(mongoc_socket_listen(pListener, 1), ==, 0);
								mongoc_socklen_t Size = sizeof(Address);
								DMibAssert(mongoc_socket_getsockname(pListener, (sockaddr *)&Address, &Size), ==, 0);
								int64 Deadline = bson_get_monotonic_time() + 5000000;
								DMibAssert(mongoc_socket_connect(pReader, (sockaddr *)&Address, Size, Deadline), ==, 0);
								pWriter = mongoc_socket_accept(pListener, Deadline);
								DMibAssertTrue(pWriter != nullptr);
								auto Handle = pReader->sd;

								if (Case == 3)
									_mongoc_interrupt_cancel(pInterrupt);

								NThread::CEvent Started;
								NThread::CEvent Completed;
								ssize_t Result = 0;
								int Events = 0;
								auto Worker = NThread::CThreadObject::fs_StartThread
									(
										[&](NThread::CThreadObject *) -> aint
										{
											Started.f_SetSignaled();
											if (Case == 1)
											{
												mongoc_socket_poll_t Poll{pReader, POLLIN, 0};
												Result = mongoc_socket_poll(&Poll, 1, 5000);
												Events = Poll.revents;
											}
											else
											{
												char Byte;
												Result = mongoc_socket_recv(pReader, &Byte, 1, 0, bson_get_monotonic_time() + 5000000);
											}
											Completed.f_SetSignaled();
											return 0;
										}
										, "Mongo socket wait"
									)
								;
								auto Join = g_OnScopeExit / [&]
									{
										_mongoc_interrupt_cancel(pInterrupt);
										Worker->f_Stop(true);
									}
								;
								Started.f_Wait();
								if (Case == 2)
								{
									DMibExpectTrue(Completed.f_WaitTimeout(1.2));
									DMibExpect(mongoc_socket_send(pWriter, "x", 1, bson_get_monotonic_time() + 1000000), ==, 1);
								}
								else
									_mongoc_interrupt_cancel(pInterrupt);

								DMibExpectFalse(Completed.f_WaitTimeout(2.0));
								Worker->f_Stop(true);
								DMibExpect(Result, ==, Case == 1 || Case == 2 ? 1 : -1);
								if (Case == 1)
									DMibExpect(Events & POLLERR, !=, 0);
								DMibExpect(pReader->sd, ==, Handle);
								int Type = 0;
								Size = sizeof(Type);
								DMibExpect(getsockopt(Handle, SOL_SOCKET, SO_TYPE, (char *)&Type, &Size), ==, 0);
								DMibExpect(Type, ==, SOCK_STREAM);
							}
						}
					)
				;
				co_return {};
			};
		}
	};

	DMibTestRegister(CSocketInterrupt_Tests, Malterlib::Mongo);
}
