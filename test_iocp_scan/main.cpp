#include <WinSock2.h>
#include <mswsock.h>
// IPV6 支持
#include <ws2tcpip.h>

#include <list>

#include <stdio.h>

#pragma comment(lib,"ws2_32")

using namespace std;

struct scan_overlapped
{
	OVERLAPPED o;

	int fd;

	int port;
};

struct scanner
{
	HANDLE *hcompletions;

	LPFN_CONNECTEX pfn_connectex;
	LPFN_DISCONNECTEX pfn_disconnectex;

	HANDLE *hthreads;

	HANDLE hevent;

	unsigned int i;

	volatile unsigned int v;

	int working;
};

DWORD WINAPI scan_thread_proc(LPVOID parameter)
{
	struct scanner *pscanner = (struct scanner *)parameter;
	HANDLE hcompletion;
	struct scan_overlapped *pso;
	OVERLAPPED *po;
	ULONG_PTR completionkey;
	DWORD numberofbytes;
	SOCKET fd;
	BOOL flag;
	unsigned int i;
	unsigned int port;

	i = pscanner->i;
	hcompletion = pscanner->hcompletions[i];

	SetEvent(pscanner->hevent);

	while (pscanner->working)
	{
		flag = GetQueuedCompletionStatus(hcompletion, &numberofbytes, &completionkey, &po, INFINITE);
		//printf("flag %d, po %p\r\n", flag, po);
		//if (flag)
		{
			if (po)
			{
				pso = (struct scan_overlapped *)CONTAINING_RECORD(po, struct scan_overlapped, o);

				port = pso->port;
				pso->fd = -1;

				fd = (SOCKET)completionkey;

				if (flag)
				{
					setsockopt(fd, SOL_SOCKET, SO_UPDATE_CONNECT_CONTEXT, NULL, 0);
				}

				if (flag)
				{
					printf("port %d\r\n", port);
				}

				pscanner->pfn_disconnectex(fd, NULL, 0, 0);

				closesocket(fd);

				InterlockedDecrement((volatile unsigned int *)&pscanner->v);
			}
		}
	}

	return(0);
}

int myconnect(list<struct scan_overlapped> *fds, struct scanner *pscanner, const struct sockaddr *psa, unsigned int len, unsigned int port,
	unsigned int i)
{
	GUID id_connectex = WSAID_CONNECTEX;
	GUID id_disconnectex = WSAID_DISCONNECTEX;
	DWORD numberofbytes = 0;
	struct scan_overlapped so;
	struct scan_overlapped *pso;
	SOCKET fd;
	int result = 0;

	fd = WSASocket(psa->sa_family, SOCK_STREAM, IPPROTO_TCP, NULL, 0, WSA_FLAG_OVERLAPPED);
	if (fd != -1 && CreateIoCompletionPort((HANDLE)fd, pscanner->hcompletions[i], (ULONG_PTR)fd, 0) == pscanner->hcompletions[i])
	{
		if ((pscanner->pfn_connectex || WSAIoctl(fd, SIO_GET_EXTENSION_FUNCTION_POINTER, &id_connectex, sizeof(id_connectex), &pscanner->pfn_connectex, sizeof(pscanner->pfn_connectex), &numberofbytes, NULL, NULL) != SOCKET_ERROR) && 
			(pscanner->pfn_disconnectex || WSAIoctl(fd, SIO_GET_EXTENSION_FUNCTION_POINTER, &id_disconnectex, sizeof(id_disconnectex), &pscanner->pfn_connectex, sizeof(pscanner->pfn_disconnectex), &numberofbytes, NULL, NULL) != SOCKET_ERROR))
		{
			list<struct scan_overlapped>::reverse_iterator rit;

			memset(&so.o, 0, sizeof(so.o));
			so.port = port;

			fds->push_back(so);
			rit = fds->rbegin();

			pso = &*rit;

			pso->fd = fd;

			struct sockaddr_in sai;

			memset(&sai, 0, sizeof(sai));
			sai.sin_family = psa->sa_family;
			sai.sin_addr.S_un.S_addr = 0;
			sai.sin_port = 0;

			bind(fd, (const struct sockaddr *)&sai, len);

			InterlockedIncrement((volatile unsigned int *)&pscanner->v);

			//if (pscanner->v > 10000)
			//{
			//	unsigned int v = pscanner->v;

			//	while (pscanner->v + 5000 > v)
			//	{
			//		Sleep(1000);
			//	}
			//}

			int errorcode;
			result = pscanner->pfn_connectex(fd, (const struct sockaddr *)psa, len, NULL, 0, NULL, &pso->o) ||
				(errorcode = WSAGetLastError()) == ERROR_IO_PENDING;

			if (result == 0)
			{
				printf("errorcode %d\r\n", errorcode);

				//closesocket(fd);
			}
		}
	}

	return(result);
}

int wmain(int argc, WCHAR *argv[])
{
	list<struct scan_overlapped> fds;
	struct scanner pscanner[1];
	unsigned int i;
	unsigned int count;

	WSADATA wsadata;

	WSAStartup(MAKEWORD(2, 2), &wsadata);

	struct sockaddr_in sai;

	memset(&sai, 0, sizeof(sai));
	sai.sin_family = AF_INET;

	pscanner->v = 0;

	char cp[256];
	if (argc > 1)
	{
		i = 0;
		while (argv[1][i] != '\0' && i + 1 < sizeof(cp))
		{
			cp[i] = argv[1][i];
			i++;
		}
		cp[i] = '\0';
	}
	sai.sin_addr.S_un.S_addr = inet_addr(cp);

	SYSTEM_INFO si;
	GetSystemInfo(&si);

	count = si.dwNumberOfProcessors;

	printf("%s, %d\r\n", cp, count);

	unsigned int tickcount0;
	unsigned int tickcount1;

	tickcount0 = GetTickCount();

	// 不作错误判断

	pscanner->pfn_connectex = NULL;
	pscanner->pfn_disconnectex = NULL;

	pscanner->working = 1;

	pscanner->hevent = CreateEvent(NULL, TRUE, FALSE, NULL);

	pscanner->hcompletions = (HANDLE *)malloc(sizeof(HANDLE)* count);
	pscanner->hthreads = (HANDLE *)malloc(sizeof(HANDLE)* count);
	for (i = 0; i < count; i++)
	{
		pscanner->hcompletions[i] = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 1);

		pscanner->i = i;
		ResetEvent(pscanner->hevent);
		if (pscanner->hthreads[i] = CreateThread(NULL, 0, scan_thread_proc, pscanner, 0, NULL))
		{
			WaitForSingleObject(pscanner->hevent, INFINITE);
		}
	}

	CloseHandle(pscanner->hevent);

	for (i = 1; i < 65536; i++)
	{
		sai.sin_port = htons(i);
		myconnect(&fds, pscanner, (const struct sockaddr *)&sai, sizeof(sai), i, i % count);
	}

	//getchar();
	while (pscanner->v)
	{
		Sleep(2000);
	}

	pscanner->working = 0;

	for (i = 0; i < count; i++)
	{
		PostQueuedCompletionStatus(pscanner->hcompletions[i], 0, (ULONG_PTR)NULL, NULL);
	}

	for (i = 0; i < count; i++)
	{
		WaitForSingleObject(pscanner->hthreads[i], INFINITE);
		CloseHandle(pscanner->hthreads[i]);
	}
	free(pscanner->hthreads);

	list<struct scan_overlapped>::iterator it;

	for (it = fds.begin(); it != fds.end(); ++it)
	{
		if (it->fd != -1)
		{
			closesocket(it->fd);
		}
	}

	tickcount1 = GetTickCount();

	printf("Cost %d\r\n", tickcount1 - tickcount0);

	WSACleanup();

	return(0);
}