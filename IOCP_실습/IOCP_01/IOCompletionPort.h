#pragma once
#pragma comment(lib,"ws2_32")
#include <winsock2.h>
#include <Ws2tcpip.h>

#include<thread>
#include<vector>

#define MAX_SOCKBUF 1024	//��Ŷ ũ��
#define MAX_WORKERTHREAD 4	//������ Ǯ�� ���� ������ ��

enum class IOOperation
{
	RECV,
	SEND
};

//WSAOVERLAPPED ����ü�� Ȯ���ؼ� �ʿ��� ������ ���ִ´�.
struct stOverlappedEx
{
	WSAOVERLAPPED	m_wsaOverlapped;		//Overlapped I/O����ü
	SOCKET			m_socketClient;			//Ŭ���̾�Ʈ ����
	WSABUF			m_wsaBuf;				//Overlapped I/O�۾� ����
	char			m_szBuf[MAX_SOCKBUF];	//������ ����
	IOOperation		m_eOperation;			// �۾� ���� ����
};

//Ŭ���̾�Ʈ ������ ��� ����ü
struct stClientInfo 
{
	SOCKET m_socketClient;
	stOverlappedEx m_stRecvOverlappedEx;
	stOverlappedEx m_stSendOverlappedEx;

	stClientInfo()
	{
		ZeroMemory(&m_stRecvOverlappedEx, sizeof(stOverlappedEx));
		ZeroMemory(&m_stSendOverlappedEx, sizeof(stOverlappedEx));
		m_socketClient = INVALID_SOCKET;
	}
};

class IOCompletionPort
{
public:
	IOCompletionPort(void){}
	~IOCompletionPort(void)
	{
		//������ ����� ������
		WSACleanup();
	}
	//������ �ʱ�ȭ �ϴ� �Լ�
	bool InitSocket()
	{
		WSADATA wsaData;

		int nRet = WSAStartup(MAKEWORD(2, 2), &wsaData);
		if (0 != nRet)
		{
			printf("[����] WSAStartup()�Լ� ���� : %d\n", WSAGetLastError());
			return false;
		}

		//���������� TPC, Overlapped I/O ���� ����
		mListenSocket = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, NULL, WSA_FLAG_OVERLAPPED);

		if (INVALID_SOCKET == mListenSocket)
		{
			printf("[����] socket()�Լ� ���� : %d\n", WSAGetLastError());
			return false;
		}

		printf("���� �ʱ�ȭ ����\n");
		return true;
	}

	//������ �Լ�
	//������ �ּ������� ���ϰ� �����Ű�� ���� ��û�� �ޱ����� ����� �Լ�
	bool BindandListen(int nBindPort)
	{
		SOCKADDR_IN stServerAddr;
		stServerAddr.sin_family = AF_INET;
		stServerAddr.sin_port = htons(nBindPort); //������Ʈ�� �����Ѵ�.
		//� �ּҿ��� ������ �����̶� �޾Ƶ��̰ڴ�.
		//���� ������� �̷��� ����, ���� �� �����ǿ����� ������ �ް� �ʹٸ�
		//�� �ּҸ� inet_addr �Լ��� �̿��� �������.
		stServerAddr.sin_addr.s_addr = htonl(INADDR_ANY);

		//������ ������ ���� �ּ� ������ IOCompletionPort ������ �����Ѵ�.
		int nRet = bind(mListenSocket, (SOCKADDR*)&stServerAddr, sizeof(SOCKADDR_IN));
		if (0 != nRet)
		{
			printf("[����] bind()�Լ� ���� : %d\n", WSAGetLastError());
		}

		//���� ��û�� �޾Ƶ��̱� ���� cIOCompletionPort ������ ����ϰ� ���Ӵ��ť�� 5���� �����Ѵ�.
		nRet = listen(mListenSocket, 5);
		if (0 != nRet)
		{
			printf("[����] listen()�Լ� ���� : %d\n", WSAGetLastError());
			return false;
		}
		printf("���� ��� ����\n");
		return true;
	}

	//���� ��û�� �����ϰ� �޼����� �޾Ƽ� ó���ϴ� �Լ�
	bool StartServer(const UINT32 maxClientCount)
	{
		CreateClient(maxClientCount);

		mIOCPHandle = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, NULL, MAX_WORKERTHREAD);
		if (NULL == mIOCPHandle)
		{
			printf("[����] CreateClient()�Լ� ���� : %d\n", WSAGetLastError());
			return false;
		}

		bool bRet = CreateWokerThread();
		if (false == bRet) {
			return false;
		}

		bRet = CreateAccepterThread();
		if (false == bRet) {
			return false;
		}

		printf("��������\n");
		return true;
	}

	//�����Ǿ��ִ� �����带 �ı��Ѵ�.
	void DestroyThread()
	{
		mIsWorkerRun = false;
		CloseHandle(mIOCPHandle);

		for (auto& th : mIOWorkerThreads)
		{
			if (th.joinable())
			{
				th.join();
			}
		}

		//Accepter �����带 �����Ѵ�.
		mIsWorkerRun = false;
		closesocket(mListenSocket);

		if (mAccepterThread.joinable())
		{
			mAccepterThread.join();
		}
	}

private:
	void CreateClient(const UINT32 maxClientCount)
	{
		for (UINT32 i = 0; i < maxClientCount; ++i)
		{
			mClientInfos.emplace_back();
		}
	}

	//WaitingThread Queue���� ����� ��������� ����
	bool CreateWokerThread()
	{
		unsigned int uiThreadld = 0;
		//WaitingThread Queue�� ��� ���·� ���� ������� ���� ����Ǵ� ����:( cpu���� * 2 ) + 1
		for (int i = 0; i < MAX_WORKERTHREAD; i++)
		{
			mIOWorkerThreads.emplace_back([this]() {WokerThread(); });
		}
		printf("WokerThread ����\n");
		return true;
	}

	//accept ��û�� ó���ϴ� ������ ����
	bool CreateAccepterThread()
	{
		mAccepterThread = std::thread([this]() {AccepterThread(); });
		printf("AccepterThread ����\n");
		return true;
	}

	//������� �ʴ� Ŭ���̾�Ʈ ���� ����ü�� ��ȯ�Ѵ�.
	stClientInfo* GetEmptyClientInfo()
	{
		for (auto& client : mClientInfos)
		{
			if (INVALID_SOCKET == client.m_socketClient)
			{
				return &client;
			}
		}
		return nullptr;
	}
	
	//CompletionPort��ü�� ���ϰ� CompletionKey�� �����Ű�� ������ �Ѵ�.
	bool BindIOCompletionPort(stClientInfo* pClientInfo)
	{
		//socket�� pClientInfo�� CompletionPort��ü�� �����Ų��.
		auto hIOCP = CreateIoCompletionPort((HANDLE)pClientInfo->m_socketClient
											, mIOCPHandle
											, (ULONG_PTR)(pClientInfo), 0);
		if (NULL == hIOCP || mIOCPHandle != hIOCP)
		{
			printf("[����] CreateIoCompletionPort()�Լ� ���� : %d\n", GetLastError());
			return false;
		}
		return true;
	}

	//WSARecv Overlapped I/O�۾��� ��Ų��.
	bool BindRecv(stClientInfo* pClientInfo)
	{
		DWORD dwFlag = 0;
		DWORD dwRecvNumBytes = 0;

		//Overlapped I/O�� ���� �� ������ ������ �ش�.
		pClientInfo->m_stRecvOverlappedEx.m_wsaBuf.len = MAX_SOCKBUF;
		pClientInfo->m_stRecvOverlappedEx.m_wsaBuf.buf = pClientInfo->m_stRecvOverlappedEx.m_szBuf;
		pClientInfo->m_stRecvOverlappedEx.m_eOperation = IOOperation::RECV;

		int nRet = WSARecv(pClientInfo->m_socketClient,
			&(pClientInfo->m_stRecvOverlappedEx.m_wsaBuf),
			1,
			&dwRecvNumBytes,
			&dwFlag,
			(LPWSAOVERLAPPED) & (pClientInfo->m_stRecvOverlappedEx),
			NULL);

		//socket_error�̸� client socket�� �������ɷ� ó���Ѵ�.
		if (nRet == SOCKET_ERROR && (WSAGetLastError() != ERROR_IO_PENDING))
		{
			printf("[����] WSARecv()�Լ� ���� : %d\n",WSAGetLastError());
			return false;
		}
		return true;
	}

	//WSASend Overlapped I/O�۾��� ��Ų��.
	bool SendMsg(stClientInfo* pClientInfo, char* pMsg, int nLen)
	{
		DWORD dwRecvNumBytes = 0;
		//���۵� �޼��� ����
		CopyMemory(pClientInfo->m_stSendOverlappedEx.m_szBuf, pMsg, nLen);

		//Overlapped I/O�� ���� �� ������ �������ش�.
		pClientInfo->m_stSendOverlappedEx.m_wsaBuf.len = nLen;
		pClientInfo->m_stSendOverlappedEx.m_wsaBuf.buf = pClientInfo->m_stSendOverlappedEx.m_szBuf;
		pClientInfo->m_stSendOverlappedEx.m_eOperation = IOOperation::SEND;

		int nRet = WSASend(pClientInfo->m_socketClient,
			&(pClientInfo-> m_stSendOverlappedEx.m_wsaBuf),
			1,
			&dwRecvNumBytes,
			0,
			(LPWSAOVERLAPPED) & (pClientInfo->m_stSendOverlappedEx),
			NULL);

		//socket_error�̸� client socket�� �������ɷ� ó���Ѵ�.
		if (nRet == SOCKET_ERROR && (WSAGetLastError() != ERROR_IO_PENDING))
		{
			printf("[����] WSASend()�Լ� ���� : %d\n", WSAGetLastError());
			return false;
		}
		return true;
	}

	//Overlapped I/O�۾��� ���� �Ϸ� �뺸�� �޾� �׿� �ش��ϴ� ó���� �ϴ� �Լ�
	void WokerThread()
	{
		//CompletionKey�� ���� ������ ����
		stClientInfo* pClientInfo = NULL;
		//�Լ� ȣ�� ���� ����
		bool bSuccess = TRUE;
		//Overlapped I/O�۾����� ���۵� ������ ũ��
		DWORD dwloSize = 0;
		//I/O �۾��� ���� ��û�� Overlapped ����ü�� ���� ������
		LPOVERLAPPED IpOverlapped = NULL;

		while (mIsWorkerRun)
		{
			//////////////////////////////////////////////////////
			//�� �Լ��� ���� ��������� WaitingThread Queue��
			//��� ���·� ���� �ȴ�.
			//�Ϸ�� Overlapped I/O�۾��� �߻��ϸ� IOCP Queue����
			//�Ϸ�� �۾��� ������ �� ó���� �Ѵ�.
			//�׸��� PostQueuedCompletionStatus()�Լ������� �����
			//�޼����� �����Ǹ� �����带 �����Ѵ�.
			//////////////////////////////////////////////////////
			bSuccess = GetQueuedCompletionStatus(mIOCPHandle,
				&dwloSize,					//���� ���۵� ����Ʈ
				(PULONG_PTR)&pClientInfo,	//Completion Key
				&IpOverlapped,				//Overlapped IO��ü
				INFINITE);					//����� �ð�

			//����� ������ ���� �޼��� ó��
			if (TRUE == bSuccess && 0 == dwloSize && NULL == IpOverlapped)
			{
				mIsWorkerRun = false;
				continue;
			}
			if (NULL == IpOverlapped)
			{
				continue;
			}

			//client�� ������ ��������
			if (FALSE == bSuccess || (0 == dwloSize && TRUE == bSuccess))
			{
				printf("socket(%d) ���� ����\n", (int)pClientInfo->m_socketClient);
				CloseSocket(pClientInfo);
				continue;
			}

			stOverlappedEx* pOverlappedEx = (stOverlappedEx*)IpOverlapped;

			//Overlapped I/O Recv�۾� ��� �� ó��
			if(IOOperation::RECV == pOverlappedEx->m_eOperation)
			{
				pOverlappedEx->m_szBuf[dwloSize] = NULL;
				printf("[����] bytes : % d , msg : %s\n", dwloSize, pOverlappedEx->m_szBuf);

				//Ŭ���̾�Ʈ�� �޼����� �����Ѵ�.
				SendMsg(pClientInfo, pOverlappedEx->m_szBuf, dwloSize);
				BindRecv(pClientInfo);
			}
			//Overlapped I/O Send �۾� ��� �� ó��
			else if (IOOperation::SEND == pOverlappedEx->m_eOperation)
			{
				printf("[�۽�] bytes : %d , msg : %s\n", dwloSize, pOverlappedEx->m_szBuf);
			}
			//���� ��Ȳ
			else
			{
				printf("socket(%d)���� ���ܻ�Ȳ\n", (int)pClientInfo->m_socketClient);
			}
		}
	}

	//������� ������ �޴� ������
	void AccepterThread()
	{
		SOCKADDR_IN stClientAddr;
		int nAddrLen = sizeof(SOCKADDR_IN);

		while (mIsAccepterRun)
		{
			//������ ���� ����ü�� �ε����� ���´�.
			stClientInfo* pClientInfo = GetEmptyClientInfo();
			if (NULL == pClientInfo)
			{
				printf("[����] Client Full\n");
				return;
			}

			//Ŭ���̾�Ʈ ���� ��û�� ���� ������ ��ٸ���.
			pClientInfo->m_socketClient = accept(mListenSocket, (SOCKADDR*)&stClientAddr, &nAddrLen);
			if (INVALID_SOCKET == pClientInfo->m_socketClient)
			{
				continue;
			}

			//I/OCompletion Port ��ü�� ������ �����Ų��.
			bool bRet = BindIOCompletionPort(pClientInfo);
			if (false == bRet)
			{
				return;
			}

			//Recv Overlapped I/O�۾��� ��û�� ���´�.
			bRet = BindRecv(pClientInfo);
			if (false == bRet)
			{
				return;
			}
			char clientIP[32] = { 0, };
			inet_ntop(AF_INET, &(stClientAddr.sin_addr), clientIP, 32 - 1);
			printf("Ŭ���̾�Ʈ ���� : IP(%s) SOCKET(%d)\n", clientIP, (int)pClientInfo->m_socketClient);

			++mClientCnt;
		}
	}

	//������ ������ ���� ��Ų��.
	void CloseSocket(stClientInfo* pClientInfo, bool blsForce = false)
	{
		struct linger stLinger = { 0,0 }; //SO_DONTLINGER�� ����
		//blsForce��  true�̸� SO_LINGER, timeout =0���� �����Ͽ� ���� ���� ��Ų��. ���� : ������ �ս��� ���� �� ����.
		if (true == blsForce)
		{
			stLinger.l_onoff = 1;
		}

		//socketClose������ ������ �ۼ����� ��� �ߴܽ�Ų��.
		shutdown(pClientInfo->m_socketClient, SD_BOTH);
		//���� �ɼ��� �����Ѵ�.
		setsockopt(pClientInfo->m_socketClient, SOL_SOCKET, SO_LINGER, (char*)&stLinger, sizeof(stLinger));
		//���� ������ ���� ��Ų��.
		closesocket(pClientInfo->m_socketClient);
		pClientInfo->m_socketClient = INVALID_SOCKET;
	}

	//Ŭ���̾�Ʈ ���� ���� ����ü
	std::vector<stClientInfo> mClientInfos;
	//Ŭ���̾�Ʈ�� ������ �ޱ����� ���� ����
	SOCKET mListenSocket = INVALID_SOCKET;
	//���ӵǾ��ִ� Ŭ���̾�Ʈ��
	int mClientCnt = 0;
	//IO Worker������
	std::vector<std::thread> mIOWorkerThreads;
	//Accept ������
	std::thread mAccepterThread;
	//CompletionPort ��ü �ڵ�
	HANDLE mIOCPHandle = INVALID_HANDLE_VALUE;
	//�۾� ������ ���� �÷���
	bool mIsWorkerRun = true;
	//���� ������ ���� �÷���
	bool mIsAccepterRun = true;
	//���� ����
	char mSocketBuf[1024] = { 0, };

};