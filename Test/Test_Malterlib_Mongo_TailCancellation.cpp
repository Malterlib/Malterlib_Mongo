// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <Mib/Test/Test>
#include <Mib/Mongo/Client>
#include <Mib/Concurrency/AsyncDestroy>
#include <Mib/Concurrency/ActorFunctorWeak>
#include <Mib/Cryptography/UUID>
#include <Mib/Encoding/JsonShortcuts>
#include <Mib/Time/Stopwatch>

namespace
{
	using namespace NMib;
	using namespace NMib::NConcurrency;
	using namespace NMib::NEncoding;
	using namespace NMib::NStr;

	struct CTailCancellation_Tests : NTest::CTest
	{
		void f_DoTests()
		{
			DMibTestSuite(NTest::CTestCategory("BlockedGetMore") << NTest::CTestGroup("Manual")) -> TCFuture<void>
			{
				CStr Endpoint = fg_GetSys()->f_GetEnvironmentVariable("MalterlibMongoTestEndpoint");
				if (Endpoint.f_IsEmpty())
					co_return DMibErrorInstance("Set MalterlibMongoTestEndpoint to a disposable MongoDB host:port");

				for (bool bDestroyActor : {false, true})
				{
					DMibTestPath(bDestroyActor ? "Actor destruction" : "Subscription cancellation");
					auto Settings = NMongo::CMongoConnectionSettings().f_ForConnectionString(Endpoint);
					Settings.m_bDirectConnection = true;
					CStr Database = "mal188_tail_cancellation";
					CStr Collection = "tail_" + NCryptography::fg_GetRandomUuidString();
					TCActor<NMongo::CMongoClientActor> Client{fg_Construct(Settings, Database), "Mongo cancellation"};
					TCActor<NMongo::CMongoClientActor> Monitor{fg_Construct(Settings, Database), "Mongo cancellation monitor"};
					auto DestroyClient = co_await fg_AsyncDestroy(Client);
					auto DestroyMonitor = co_await fg_AsyncDestroy(Monitor);

					CEJsonOrdered Create = {"create"_o= Collection, "capped"_o= true, "size"_o= 1048576};
					co_await Client(&NMongo::CMongoClientActor::f_RunCommand, Database, fg_Move(Create));
					CEJsonOrdered Seed = {"seq"_o= 0};
					CEJsonOrdered Insert = {"insert"_o= Collection, "documents"_o= _o[Seed]};
					co_await Client(&NMongo::CMongoClientActor::f_RunCommand, Database, fg_Move(Insert));

					NMongo::CMongoClientActor::CTailQueryParams Params;
					Params.m_Collection = Collection;
					Params.m_OrderBy = "seq";
					Params.m_Query["seq"]["$gte"] = 0;
					auto OnData = g_ActorFunctorWeak / [](CEJsonOrdered) -> TCFuture<void>
						{
							co_return {};
						}
					;
					auto Subscription = co_await Client(&NMongo::CMongoClientActor::f_TailQuery, fg_Move(Params), fg_Move(OnData));

					bool bWaiting = false;
					for (umint Attempt = 0; Attempt < 1000 && !bWaiting; ++Attempt)
					{
						CEJsonOrdered CurrentOp = {"currentOp"_o= 1, "ns"_o= Database + "." + Collection, "op"_o= "getmore"};
						auto Operations = co_await Monitor(&NMongo::CMongoClientActor::f_RunCommand, CStr("admin"), fg_Move(CurrentOp));
						bWaiting = !Operations["inprog"].f_Array().f_IsEmpty();
						if (!bWaiting)
							co_await fg_Timeout(0.01);
					}
					DMibExpectTrue(bWaiting);

					NTime::CStopwatch Stopwatch;
					Stopwatch.f_Start();
					if (bDestroyActor)
						co_await fg_Move(Client).f_Destroy().f_Timeout(5.0, "Mongo tail actor did not stop");
					else
						co_await Subscription->f_Destroy().f_Timeout(5.0, "Mongo tail subscription did not stop");
					DMibExpect(Stopwatch.f_GetTime(), <, 5.0);
					Subscription.f_Clear();

					CEJsonOrdered Drop = {"drop"_o= Collection};
					co_await Monitor(&NMongo::CMongoClientActor::f_RunCommand, Database, fg_Move(Drop));
				}

				co_return {};
			};
		}
	};

	DMibTestRegister(CTailCancellation_Tests, Malterlib::Mongo);
}
