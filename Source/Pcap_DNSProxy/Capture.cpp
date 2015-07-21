﻿// This code is part of Pcap_DNSProxy
// A local DNS server based on WinPcap and LibPcap
// Copyright (C) 2012-2015 Chengr28
// 
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either
// version 2 of the License, or (at your option) any later version.
// 
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
// 
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.


#include "Capture.h"

#if defined(ENABLE_PCAP)
//Capture initialization
void __fastcall CaptureInit(void)
{
//Initialization
	std::shared_ptr<char> ErrBuffer(new char[PCAP_ERRBUF_SIZE]());
	memset(ErrBuffer.get(), 0, PCAP_ERRBUF_SIZE);
	CaptureFilterRulesInit(PcapFilterRules);
	std::wstring wErrBuffer;
	pcap_if *pThedevs = nullptr, *pDrive = nullptr;
	std::vector<std::string>::iterator CaptureIter;
	auto IsErrorFirstPrint = true;

//Capture Monitor
	std::unique_lock<std::mutex> CaptureMutex(CaptureLock);
	CaptureMutex.unlock();
	for (;;)
	{
	//Open all devices.
		if (pcap_findalldevs(&pThedevs, ErrBuffer.get()) == PCAP_ERROR)
		{
			if (MBSToWCSString(wErrBuffer, ErrBuffer.get()))
			{
				PrintError(LOG_ERROR_PCAP, wErrBuffer.c_str(), 0, nullptr, 0);
				wErrBuffer.clear();
			}

			memset(ErrBuffer.get(), 0, PCAP_ERRBUF_SIZE);
			Sleep(MONITOR_LOOP_INTERVAL_TIME);
			continue;
		}

	//Permissions check and check available network devices.
		if (pThedevs == nullptr)
		{
			if (IsErrorFirstPrint)
				IsErrorFirstPrint = false;
			else 
				PrintError(LOG_ERROR_PCAP, L"Insufficient privileges or not any available network devices", 0, nullptr, 0);

			Sleep(MONITOR_LOOP_INTERVAL_TIME);
			continue;
		}
		else {
		//Mark captures.
			if (PcapRunningList.empty())
			{
				std::thread CaptureThread(CaptureModule, pThedevs, true);
				CaptureThread.detach();
			}
			else {
				pDrive = pThedevs;

			//Scan all devices.
				while (pDrive != nullptr)
				{
					CaptureMutex.lock();
					for (CaptureIter = PcapRunningList.begin();CaptureIter != PcapRunningList.end();++CaptureIter)
					{
						if (*CaptureIter == pDrive->name)
						{
							break;
						}
						else if (CaptureIter + 1U == PcapRunningList.end())
						{
							std::thread CaptureThread(CaptureModule, pDrive, false);
							CaptureThread.detach();

							break;
						}
					}
					CaptureMutex.unlock();
					pDrive = pDrive->next;
				}
			}
		}

		Sleep(MONITOR_LOOP_INTERVAL_TIME);
		pcap_freealldevs(pThedevs);
	}

//Monitor terminated
	PrintError(LOG_ERROR_SYSTEM, L"Capture module Monitor terminated", 0, nullptr, 0);
	return;
}

void __fastcall CaptureFilterRulesInit(std::string &FilterRules)
{
//Initialization
	std::string AddressesString;
	std::shared_ptr<char> Addr(new char[ADDR_STRING_MAXSIZE]());
	memset(Addr.get(), 0, ADDR_STRING_MAXSIZE);
	FilterRules.clear();

//Minimum supported system of inet_ntop() and inet_pton() is Windows Vista(Windows XP with SP3 support). [Roy Tam]
#if (defined(PLATFORM_WIN32) && !defined(PLATFORM_WIN64))
	std::shared_ptr<sockaddr_storage> SockAddr(new sockaddr_storage());
	memset(SockAddr.get(), 0, sizeof(sockaddr_storage));
	DWORD BufferLength = ADDR_STRING_MAXSIZE;
#endif

	FilterRules.append("(src host ");

//Set capture filter.
	auto IsNoSingle = false, IsConnection = false;
	if (Parameter.DNSTarget.IPv6.AddressData.Storage.ss_family > 0 && Parameter.DNSTarget.IPv4.AddressData.Storage.ss_family > 0 || 
		Parameter.DNSTarget.Alternate_IPv6.AddressData.Storage.ss_family > 0 || Parameter.DNSTarget.Alternate_IPv4.AddressData.Storage.ss_family > 0)
	{
		IsNoSingle = true;
		AddressesString = ("(");
	}

	//Main(IPv6)
	if (Parameter.DNSTarget.IPv6.AddressData.Storage.ss_family > 0 && 
		(Parameter.RequestMode_Network == REQUEST_MODE_NETWORK_BOTH || Parameter.RequestMode_Network == REQUEST_MODE_IPV6 || //IPv6
		Parameter.RequestMode_Network == REQUEST_MODE_IPV4 && Parameter.DNSTarget.IPv4.AddressData.Storage.ss_family == 0)) //Non-IPv4
	{
	#if (defined(PLATFORM_WIN32) && !defined(PLATFORM_WIN64))
		if (Parameter.Inet_Ntop_PTR != nullptr)
		{
			(*Parameter.Inet_Ntop_PTR)(AF_INET6, &Parameter.DNSTarget.IPv6.AddressData.IPv6.sin6_addr, Addr.get(), ADDR_STRING_MAXSIZE);
		}
		else {
			BufferLength = ADDR_STRING_MAXSIZE;
			SockAddr->ss_family = AF_INET6;
			((PSOCKADDR_IN6)SockAddr.get())->sin6_addr = Parameter.DNSTarget.IPv6.AddressData.IPv6.sin6_addr;
			WSAAddressToStringA((PSOCKADDR)SockAddr.get(), sizeof(sockaddr_in6), nullptr, Addr.get(), &BufferLength);
		}
	#else
		inet_ntop(AF_INET6, &Parameter.DNSTarget.IPv6.AddressData.IPv6.sin6_addr, Addr.get(), ADDR_STRING_MAXSIZE);
	#endif

		if (!IsConnection)
			IsConnection = true;
		else 
			AddressesString.append(" or ");
		AddressesString.append(Addr.get());
		memset(Addr.get(), 0, ADDR_STRING_MAXSIZE);
	}
	//Alternate(IPv6)
	if (Parameter.DNSTarget.Alternate_IPv6.AddressData.Storage.ss_family > 0 && 
		(Parameter.RequestMode_Network == REQUEST_MODE_NETWORK_BOTH || Parameter.RequestMode_Network == REQUEST_MODE_IPV6 || //IPv6
		Parameter.RequestMode_Network == REQUEST_MODE_IPV4 && Parameter.DNSTarget.IPv4.AddressData.Storage.ss_family == 0)) //Non-IPv4
	{
	#if (defined(PLATFORM_WIN32) && !defined(PLATFORM_WIN64))
		if (Parameter.Inet_Ntop_PTR != nullptr)
		{
			(*Parameter.Inet_Ntop_PTR)(AF_INET6, &Parameter.DNSTarget.Alternate_IPv6.AddressData.IPv6.sin6_addr, Addr.get(), ADDR_STRING_MAXSIZE);
		}
		else {
			BufferLength = ADDR_STRING_MAXSIZE;
			SockAddr->ss_family = AF_INET6;
			((PSOCKADDR_IN6)SockAddr.get())->sin6_addr = Parameter.DNSTarget.Alternate_IPv6.AddressData.IPv6.sin6_addr;
			WSAAddressToStringA((PSOCKADDR)SockAddr.get(), sizeof(sockaddr_in6), nullptr, Addr.get(), &BufferLength);
		}
	#else
		inet_ntop(AF_INET6, &Parameter.DNSTarget.Alternate_IPv6.AddressData.IPv6.sin6_addr, Addr.get(), ADDR_STRING_MAXSIZE);
	#endif

		if (!IsConnection)
			IsConnection = true;
		else 
			AddressesString.append(" or ");
		AddressesString.append(Addr.get());
		memset(Addr.get(), 0, ADDR_STRING_MAXSIZE);
	}
	//Other(Multi/IPv6)
	if (Parameter.DNSTarget.IPv6_Multi != nullptr && 
		(Parameter.RequestMode_Network == REQUEST_MODE_NETWORK_BOTH || Parameter.RequestMode_Network == REQUEST_MODE_IPV6 || //IPv6
		Parameter.RequestMode_Network == REQUEST_MODE_IPV4 && Parameter.DNSTarget.IPv4.AddressData.Storage.ss_family == 0)) //Non-IPv4
	{
		for (auto DNSServerDataIter:*Parameter.DNSTarget.IPv6_Multi)
		{
		#if (defined(PLATFORM_WIN32) && !defined(PLATFORM_WIN64))
			if (Parameter.Inet_Ntop_PTR != nullptr)
			{
				(*Parameter.Inet_Ntop_PTR)(AF_INET6, &DNSServerDataIter.AddressData.IPv6.sin6_addr, Addr.get(), ADDR_STRING_MAXSIZE);
			}
			else {
				BufferLength = ADDR_STRING_MAXSIZE;
				SockAddr->ss_family = AF_INET6;
				((PSOCKADDR_IN6)SockAddr.get())->sin6_addr = DNSServerDataIter.AddressData.IPv6.sin6_addr;
				WSAAddressToStringA((PSOCKADDR)SockAddr.get(), sizeof(sockaddr_in6), nullptr, Addr.get(), &BufferLength);
			}
		#else
			inet_ntop(AF_INET6, &DNSServerDataIter.AddressData.IPv6.sin6_addr, Addr.get(), ADDR_STRING_MAXSIZE);
		#endif

			if (!IsConnection)
				IsConnection = true;
			else 
				AddressesString.append(" or ");
			AddressesString.append(Addr.get());
			memset(Addr.get(), 0, ADDR_STRING_MAXSIZE);
		}
	}
	//Main(IPv4)
	if (Parameter.DNSTarget.IPv4.AddressData.Storage.ss_family > 0 && 
		(Parameter.RequestMode_Network == REQUEST_MODE_NETWORK_BOTH || Parameter.RequestMode_Network == REQUEST_MODE_IPV4 || //IPv4
		Parameter.RequestMode_Network == REQUEST_MODE_IPV6 && Parameter.DNSTarget.IPv6.AddressData.Storage.ss_family == 0)) //Non-IPv6
	{
	#if (defined(PLATFORM_WIN32) && !defined(PLATFORM_WIN64))
		if (Parameter.Inet_Ntop_PTR != nullptr)
		{
			(*Parameter.Inet_Ntop_PTR)(AF_INET, &Parameter.DNSTarget.IPv4.AddressData.IPv4.sin_addr, Addr.get(), ADDR_STRING_MAXSIZE);
		}
		else {
			BufferLength = ADDR_STRING_MAXSIZE;
			SockAddr->ss_family = AF_INET;
			((PSOCKADDR_IN)SockAddr.get())->sin_addr = Parameter.DNSTarget.IPv4.AddressData.IPv4.sin_addr;
			WSAAddressToStringA((PSOCKADDR)SockAddr.get(), sizeof(sockaddr_in), nullptr, Addr.get(), &BufferLength);
		}
	#else
		inet_ntop(AF_INET, &Parameter.DNSTarget.IPv4.AddressData.IPv4.sin_addr, Addr.get(), ADDR_STRING_MAXSIZE);
	#endif

		if (!IsConnection)
			IsConnection = true;
		else 
			AddressesString.append(" or ");
		AddressesString.append(Addr.get());
		memset(Addr.get(), 0, ADDR_STRING_MAXSIZE);
	}
	//Alternate(IPv4)
	if (Parameter.DNSTarget.Alternate_IPv4.AddressData.Storage.ss_family > 0 && 
		(Parameter.RequestMode_Network == REQUEST_MODE_NETWORK_BOTH || Parameter.RequestMode_Network == REQUEST_MODE_IPV4 || //IPv4
		Parameter.RequestMode_Network == REQUEST_MODE_IPV6 && Parameter.DNSTarget.IPv6.AddressData.Storage.ss_family == 0)) //Non-IPv6
	{
	#if (defined(PLATFORM_WIN32) && !defined(PLATFORM_WIN64))
		if (Parameter.Inet_Ntop_PTR != nullptr)
		{
			(*Parameter.Inet_Ntop_PTR)(AF_INET, &Parameter.DNSTarget.Alternate_IPv4.AddressData.IPv4.sin_addr, Addr.get(), ADDR_STRING_MAXSIZE);
		}
		else {
			BufferLength = ADDR_STRING_MAXSIZE;
			SockAddr->ss_family = AF_INET;
			((PSOCKADDR_IN)SockAddr.get())->sin_addr = Parameter.DNSTarget.Alternate_IPv4.AddressData.IPv4.sin_addr;
			WSAAddressToStringA((PSOCKADDR)SockAddr.get(), sizeof(sockaddr_in), nullptr, Addr.get(), &BufferLength);
		}
	#else
		inet_ntop(AF_INET, &Parameter.DNSTarget.Alternate_IPv4.AddressData.IPv4.sin_addr, Addr.get(), ADDR_STRING_MAXSIZE);
	#endif

		if (!IsConnection)
			IsConnection = true;
		else 
			AddressesString.append(" or ");
		AddressesString.append(Addr.get());
		memset(Addr.get(), 0, ADDR_STRING_MAXSIZE);
	}
	//Other(Multi/IPv4)
	if (Parameter.DNSTarget.IPv4_Multi != nullptr && 
		(Parameter.RequestMode_Network == REQUEST_MODE_NETWORK_BOTH || Parameter.RequestMode_Network == REQUEST_MODE_IPV4 || //IPv4
		Parameter.RequestMode_Network == REQUEST_MODE_IPV6 && Parameter.DNSTarget.IPv6.AddressData.Storage.ss_family == 0)) //Non-IPv6
	{
		for (auto DNSServerDataIter:*Parameter.DNSTarget.IPv4_Multi)
		{
		#if (defined(PLATFORM_WIN32) && !defined(PLATFORM_WIN64))
			if (Parameter.Inet_Ntop_PTR != nullptr)
			{
				(*Parameter.Inet_Ntop_PTR)(AF_INET, &DNSServerDataIter.AddressData.IPv4.sin_addr, Addr.get(), ADDR_STRING_MAXSIZE);
			}
			else {
				BufferLength = ADDR_STRING_MAXSIZE;
				SockAddr->ss_family = AF_INET;
				((PSOCKADDR_IN)SockAddr.get())->sin_addr = DNSServerDataIter.AddressData.IPv4.sin_addr;
				WSAAddressToStringA((PSOCKADDR)SockAddr.get(), sizeof(sockaddr_in), nullptr, Addr.get(), &BufferLength);
			}
		#else
			inet_ntop(AF_INET, &DNSServerDataIter.AddressData.IPv4.sin_addr, Addr.get(), ADDR_STRING_MAXSIZE);
		#endif

			if (!IsConnection)
				IsConnection = true;
			else 
				AddressesString.append(" or ");
			AddressesString.append(Addr.get());
			memset(Addr.get(), 0, ADDR_STRING_MAXSIZE);
		}
	}

//End of address list
	if (IsNoSingle)
		AddressesString.append(")");
	FilterRules.append(AddressesString);
	FilterRules.append(") or (pppoes and src host ");
	FilterRules.append(AddressesString);
	FilterRules.append(")");

	return;
}

//Capture process
bool __fastcall CaptureModule(const pcap_if *pDrive, const bool IsCaptureList)
{
//Devices check
	if (pDrive->name == nullptr || pDrive->addresses == nullptr || pDrive->flags == PCAP_IF_LOOPBACK
/* Old version(2015-07-12)
	#if defined(PLATFORM_LINUX)
		|| strnlen(pDrive->name, PACKET_MAXSIZE) >= strlen("lo") && memcmp(pDrive->name, "lo", strlen("lo")) == 0
		|| strnlen(pDrive->name, PACKET_MAXSIZE) >= strlen("any") && memcmp(pDrive->name, "any", strlen("any")) == 0
		#if defined(PLATFORM_OPENWRT)
			|| strnlen(pDrive->name, PACKET_MAXSIZE) >= strlen("pppoe-wan") && memcmp(pDrive->name, "pppoe-wan", strlen("pppoe-wan")) == 0
		#endif
	#endif
*/
		)
	{
		if (IsCaptureList && pDrive->next != nullptr)
		{
			std::thread CaptureThread(CaptureModule, pDrive->next, true);
			CaptureThread.detach();
		}

		return true;
	}

//Initialization(Part 1)
	pcap_t *DeviceHandle = nullptr;
/* Old version(2015-06-27)
	std::shared_ptr<char> Buffer(new char[ORIGINAL_PACKET_MAXSIZE * Parameter.BufferQueueSize]());
	memset(Buffer.get(), 0, ORIGINAL_PACKET_MAXSIZE * Parameter.BufferQueueSize);
*/
	std::shared_ptr<char> Buffer(new char[ORIGINAL_PACKET_MAXSIZE + sizeof(uint16_t)]()); //Reserved 2 bytes for TCP header length.
	memset(Buffer.get(), 0, ORIGINAL_PACKET_MAXSIZE + sizeof(uint16_t));

//Open device
#if defined(PLATFORM_WIN)
	if ((DeviceHandle = pcap_open(pDrive->name, ORIGINAL_PACKET_MAXSIZE, PCAP_OPENFLAG_NOCAPTURE_LOCAL, (int)Parameter.PcapReadingTimeout, nullptr, Buffer.get())) == nullptr)
#elif (defined(PLATFORM_LINUX) || defined(PLATFORM_MACX))
	if ((DeviceHandle = pcap_open_live(pDrive->name, ORIGINAL_PACKET_MAXSIZE, 0, (int)Parameter.PcapReadingTimeout, Buffer.get())) == nullptr)
#endif
	{
		std::wstring ErrBuffer;
		if (MBSToWCSString(ErrBuffer, Buffer.get()))
			PrintError(LOG_ERROR_PCAP, ErrBuffer.c_str(), 0, nullptr, 0);

		return false;
	}

//Check device name.
/* Old version(2015-07-09)
	std::wstring DeviceName;
	MBSToWCSString(DeviceName, pDrive->name);
	if (DeviceName.empty())
		DeviceName = L"<Error device name>";
*/
//Check device type.
	uint16_t DeviceType = 0;
	if (pcap_datalink(DeviceHandle) == DLT_EN10MB || pcap_datalink(DeviceHandle) == DLT_PPP_ETHER || pcap_datalink(DeviceHandle) == DLT_EN3MB) //Ethernet II(Including PPPoE)
		DeviceType = DLT_EN10MB;
	else if (pcap_datalink(DeviceHandle) == DLT_APPLE_IP_OVER_IEEE1394) //Apple IEEE 1394
		DeviceType = DLT_APPLE_IP_OVER_IEEE1394;
	if (DeviceType == 0)
	{
/* Old version(2015-07-09)
	#if (defined(PLATFORM_LINUX) || defined(PLATFORM_MACX))
		if (pcap_datalink(DeviceHandle) != DLT_NULL && pcap_datalink(DeviceHandle) != DLT_NFLOG) //BSD loopback encapsulation and Linux NETLINK NFLOG socket log messages
		{
	#endif
			std::shared_ptr<wchar_t> ErrBuffer(new wchar_t[PCAP_ERRBUF_SIZE]());
			wmemset(ErrBuffer.get(), 0, PCAP_ERRBUF_SIZE);
			wcsncpy_s(ErrBuffer.get(), PCAP_ERRBUF_SIZE, DeviceName.c_str(), DeviceName.length());
			wcsncpy_s(ErrBuffer.get() + wcsnlen_s(DeviceName.c_str(), PCAP_ERRBUF_SIZE), PCAP_ERRBUF_SIZE - DeviceName.length(), L" is not supported", wcslen(L" is not supported"));
			PrintError(LOG_ERROR_PCAP, ErrBuffer.get(), 0, nullptr, 0);
	#if (defined(PLATFORM_LINUX) || defined(PLATFORM_MACX))
		}
	#endif
*/
		pcap_close(DeviceHandle);
		return false;
	}

//Compile the string into a filter program.
	std::shared_ptr<bpf_program> BPF_Code(new bpf_program());
	memset(BPF_Code.get(), 0, sizeof(bpf_program));
#if defined(PLATFORM_WIN)
	if (pcap_compile(DeviceHandle, BPF_Code.get(), PcapFilterRules.c_str(), PCAP_COMPILE_OPTIMIZE, (bpf_u_int32)pDrive->addresses->netmask) == PCAP_ERROR)
#elif (defined(PLATFORM_LINUX) || defined(PLATFORM_MACX))
	if (pcap_compile(DeviceHandle, BPF_Code.get(), PcapFilterRules.c_str(), PCAP_COMPILE_OPTIMIZE, 0) == PCAP_ERROR)
#endif
	{
		std::wstring ErrBuffer;
		if (MBSToWCSString(ErrBuffer, pcap_geterr(DeviceHandle)))
			PrintError(LOG_ERROR_PCAP, ErrBuffer.c_str(), 0, nullptr, 0);

		pcap_close(DeviceHandle);
		return false;
	}

//Specify a filter program.
	if (pcap_setfilter(DeviceHandle, BPF_Code.get()) == PCAP_ERROR)
	{
		std::wstring ErrBuffer;
		if (MBSToWCSString(ErrBuffer, pcap_geterr(DeviceHandle)))
			PrintError(LOG_ERROR_PCAP, ErrBuffer.c_str(), 0, nullptr, 0);

		pcap_freecode(BPF_Code.get());
		pcap_close(DeviceHandle);
		return false;
	}

//Start captures with other devices.
	std::string CaptureDevice(pDrive->name);
	PcapRunningList.push_back(CaptureDevice);
	if (IsCaptureList && pDrive->next != nullptr)
	{
		std::thread CaptureThread(CaptureModule, pDrive->next, IsCaptureList);
		CaptureThread.detach();
	}

/* Old version(2015-06-27)
	size_t Index = 0;
	pcap_pkthdr *PacketHeader = nullptr;
	const unsigned char *PacketData = nullptr;
	for (;;)
	{
		Result = pcap_next_ex(DeviceHandle, &PacketHeader, &PacketData);
		switch (Result)
		{
			case PCAP_ERROR: //An error occurred.
			{

				std::shared_ptr<wchar_t> ErrBuffer(new wchar_t[PCAP_ERRBUF_SIZE]());
				wmemset(ErrBuffer.get(), 0, PCAP_ERRBUF_SIZE);

			//Devices are offline or other errors, wait for retrying.
				wcsncpy_s(ErrBuffer.get(), PCAP_ERRBUF_SIZE, L"An error occurred in ", wcslen(L"An error occurred in "));
				wcsncpy_s(ErrBuffer.get() + wcslen(L"An error occurred in "), PCAP_ERRBUF_SIZE - wcslen(L"An error occurred in "), DeviceName.c_str(), DeviceName.length());
				PrintError(LOG_ERROR_PCAP, ErrBuffer.get(), 0, nullptr, 0);

			//Delete from devices list.
				std::unique_lock<std::mutex> CaptureMutex(CaptureLock);
				for (auto CaptureIter = PcapRunningList.begin();CaptureIter != PcapRunningList.end();)
				{
					if (*CaptureIter == CaptureDevice)
					{
						CaptureIter = PcapRunningList.erase(CaptureIter);
						if (CaptureIter == PcapRunningList.end())
							break;
					}
					else {
						++CaptureIter;
					}
				}
				PcapRunningList.shrink_to_fit();
				CaptureMutex.unlock();
				
				pcap_freecode(BPF_Code.get());
				pcap_close(DeviceHandle);
				return false;
			}break;
			case PCAP_ERROR_BREAK:
			{
				std::shared_ptr<wchar_t> ErrBuffer(new wchar_t[PCAP_ERRBUF_SIZE]());
				wmemset(ErrBuffer.get(), 0, PCAP_ERRBUF_SIZE);

			//Devices are offline or other errors, wait for retrying.
				wcsncpy_s(ErrBuffer.get(), PCAP_ERRBUF_SIZE, L"EOF was reached reading from an offline capture in ", wcslen(L"EOF was reached reading from an offline capture in "));
				wcsncpy_s(ErrBuffer.get() + wcslen(L"EOF was reached reading from an offline capture in "), PCAP_ERRBUF_SIZE - wcslen(L"EOF was reached reading from an offline capture in "), DeviceName.c_str(), DeviceName.length());
				PrintError(LOG_ERROR_PCAP, ErrBuffer.get(), 0, nullptr, 0);

			//Delete from devices list.
				std::unique_lock<std::mutex> CaptureMutex(CaptureLock);
				for (auto CaptureIter = PcapRunningList.begin();CaptureIter != PcapRunningList.end();)
				{
					if (*CaptureIter == CaptureDevice)
					{
						CaptureIter = PcapRunningList.erase(CaptureIter);
						if (CaptureIter == PcapRunningList.end())
							break;
					}
					else {
						++CaptureIter;
					}
				}
				PcapRunningList.shrink_to_fit();
				CaptureMutex.unlock();
				
				pcap_freecode(BPF_Code.get());
				pcap_close(DeviceHandle);
				return false;
			}break;
			case PCAP_READ_TIMEOUT:
			{
				Sleep(LOOP_INTERVAL_TIME);
				continue;
			}break;
			case PCAP_READ_SUCCESS:
			{
//				memset(Buffer.get() + ORIGINAL_PACKET_MAXSIZE * Index, 0, ORIGINAL_PACKET_MAXSIZE);
				memset(Buffer.get(), 0, ORIGINAL_PACKET_MAXSIZE);

			//PPP(Such as ADSL, a part of organization networks)
				if (DeviceType == DLT_EN10MB && ((peth_hdr)PacketData)->Type == htons(OSI_L2_PPPS) && PacketHeader->caplen > sizeof(eth_hdr) + sizeof(ppp_hdr) || //PPP over Ethernet II
					DeviceType == DLT_APPLE_IP_OVER_IEEE1394 && ((pieee_1394_hdr)PacketData)->Type == htons(OSI_L2_PPPS) && PacketHeader->caplen > sizeof(ieee_1394_hdr) + sizeof(ppp_hdr)) //PPP over Apple IEEE 1394
				{
					size_t HeaderLength = 0;
					if (DeviceType == DLT_EN10MB)
						HeaderLength += sizeof(eth_hdr);
					else if (DeviceType == DLT_APPLE_IP_OVER_IEEE1394)
						HeaderLength += sizeof(ieee_1394_hdr);
					
					auto InnerHeader = (pppp_hdr)(PacketData + HeaderLength);
					if (
					//IPv6 over PPP
						InnerHeader->Protocol == htons(PPP_IPV6) && Parameter.DNSTarget.IPv6.AddressData.Storage.ss_family > 0 && 
						PacketHeader->caplen > HeaderLength + sizeof(ppp_hdr) + sizeof(ipv6_hdr) || 
					//IPv4 over PPP
						InnerHeader->Protocol == htons(PPP_IPV4) && Parameter.DNSTarget.IPv4.AddressData.Storage.ss_family > 0 && 
						PacketHeader->caplen > HeaderLength + sizeof(ppp_hdr) + sizeof(ipv4_hdr))
					{
						HeaderLength += sizeof(ppp_hdr);
//						memcpy_s(Buffer.get() + ORIGINAL_PACKET_MAXSIZE * Index, ORIGINAL_PACKET_MAXSIZE, PacketData + HeaderLength, PacketHeader->caplen - HeaderLength);
//						std::thread CaptureNetworkLayerThread(CaptureNetworkLayer, Buffer.get() + ORIGINAL_PACKET_MAXSIZE * Index, PacketHeader->caplen - HeaderLength, ntohs(InnerHeader->Protocol));
//						CaptureNetworkLayerThread.detach();
						memcpy_s(Buffer.get(), ORIGINAL_PACKET_MAXSIZE, PacketData + HeaderLength, PacketHeader->caplen - HeaderLength);
						CaptureNetworkLayer(Buffer.get(), PacketHeader->caplen - HeaderLength, ntohs(InnerHeader->Protocol));

//						Index = (Index + 1U) % Parameter.BufferQueueSize;
					}
				}

			//LAN/WLAN/IEEE 802.1X, some Mobile Communications Standard/MCS drives which disguise as a LAN
				else if (
				//IPv6 over Ethernet II
					(DeviceType == DLT_EN10MB && (((peth_hdr)PacketData)->Type == htons(OSI_L2_IPV6) && Parameter.DNSTarget.IPv6.AddressData.Storage.ss_family > 0 && 
					PacketHeader->caplen > sizeof(eth_hdr) + sizeof(ipv6_hdr) || 
				//IPv4 over Ethernet II
					((peth_hdr)PacketData)->Type == htons(OSI_L2_IPV4) && Parameter.DNSTarget.IPv4.AddressData.Storage.ss_family > 0 && 
					PacketHeader->caplen > sizeof(eth_hdr) + sizeof(ipv4_hdr))) || 
				//IPv6 over Apple IEEE 1394
					(DeviceType == DLT_APPLE_IP_OVER_IEEE1394 && ((((pieee_1394_hdr)PacketData)->Type == htons(OSI_L2_IPV6) && 
					Parameter.DNSTarget.IPv6.AddressData.Storage.ss_family > 0 && PacketHeader->caplen > sizeof(ieee_1394_hdr) + sizeof(ipv6_hdr) || 
				//IPv4 over Apple IEEE 1394
					((pieee_1394_hdr)PacketData)->Type == htons(OSI_L2_IPV4) && Parameter.DNSTarget.IPv4.AddressData.Storage.ss_family > 0 && 
					PacketHeader->caplen > sizeof(ipv4_hdr)))))
				{
					if (DeviceType == DLT_EN10MB)
					{
//						memcpy_s(Buffer.get() + ORIGINAL_PACKET_MAXSIZE * Index, ORIGINAL_PACKET_MAXSIZE, PacketData + sizeof(eth_hdr), PacketHeader->caplen - sizeof(eth_hdr));
//						std::thread CaptureNetworkLayerThread(CaptureNetworkLayer, Buffer.get() + ORIGINAL_PACKET_MAXSIZE * Index, PacketHeader->caplen - sizeof(eth_hdr), ntohs(((peth_hdr)PacketData)->Type));
//						CaptureNetworkLayerThread.detach();
						memcpy_s(Buffer.get(), ORIGINAL_PACKET_MAXSIZE, PacketData + sizeof(eth_hdr), PacketHeader->caplen - sizeof(eth_hdr));
						CaptureNetworkLayer(Buffer.get(), PacketHeader->caplen - sizeof(eth_hdr), ntohs(((peth_hdr)PacketData)->Type));
					}
					else if (DeviceType == DLT_APPLE_IP_OVER_IEEE1394)
					{
//						memcpy_s(Buffer.get() + ORIGINAL_PACKET_MAXSIZE * Index, ORIGINAL_PACKET_MAXSIZE, PacketData + sizeof(ieee_1394_hdr), PacketHeader->caplen - sizeof(ieee_1394_hdr));
//						std::thread CaptureNetworkLayerThread(CaptureNetworkLayer, Buffer.get() + ORIGINAL_PACKET_MAXSIZE * Index, PacketHeader->caplen - sizeof(ieee_1394_hdr), ntohs(((pieee_1394_hdr)PacketData)->Type));
//						CaptureNetworkLayerThread.detach();
						memcpy_s(Buffer.get(), ORIGINAL_PACKET_MAXSIZE, PacketData + sizeof(ieee_1394_hdr), PacketHeader->caplen - sizeof(ieee_1394_hdr));
						CaptureNetworkLayer(Buffer.get(), PacketHeader->caplen - sizeof(ieee_1394_hdr), ntohs(((pieee_1394_hdr)PacketData)->Type));
					}

//					Index = (Index + 1U) % Parameter.BufferQueueSize;
				}
				else {
					Sleep(LOOP_INTERVAL_TIME);
					continue;
				}
			}break;
			default: {
				if (Result == PCAP_ERROR_NO_SUCH_DEVICE || Result == PCAP_ERROR_RFMON_NOTSUP || Result == PCAP_ERROR_NOT_RFMON)
				{
				//Delete from devices list.
					std::unique_lock<std::mutex> CaptureMutex(CaptureLock);
					for (auto CaptureIter = PcapRunningList.begin();CaptureIter != PcapRunningList.end();)
					{
						if (*CaptureIter == CaptureDevice)
						{
							CaptureIter = PcapRunningList.erase(CaptureIter);
							if (CaptureIter == PcapRunningList.end())
								break;
						}
						else {
							++CaptureIter;
						}
					}
					PcapRunningList.shrink_to_fit();
					CaptureMutex.unlock();

				//Exit this capture thread.
					pcap_freecode(BPF_Code.get());
					pcap_close(DeviceHandle);
					return true;
				}
				else {
					Sleep(MONITOR_LOOP_INTERVAL_TIME);
					continue;
				}
			}
		}
	}
*/
//Initialization(Part 2)
	std::shared_ptr<CAPTURE_HANDLER_PARAM> ParamList(new CAPTURE_HANDLER_PARAM());
	memset(ParamList.get(), 0, sizeof(CAPTURE_HANDLER_PARAM));
	ParamList->Buffer = Buffer.get();
	ParamList->DeviceType = DeviceType;
	SSIZE_T Result = 0;
	std::unique_lock<std::mutex> CaptureMutex(CaptureLock);
	CaptureMutex.unlock();

//Start monitor.
	for (;;)
	{
		Result = pcap_loop(DeviceHandle, PCAP_LOOP_INFINITY, CaptureHandler, (PUCHAR)ParamList.get());
//		if (Result == PCAP_ERROR || Result == PCAP_ERROR_BREAK || Result == PCAP_ERROR_NO_SUCH_DEVICE || Result == PCAP_ERROR_RFMON_NOTSUP || Result == PCAP_ERROR_NOT_RFMON)
		if (Result < 0)
		{
		//Delete this capture from devices list.
			CaptureMutex.lock();
			for (auto CaptureIter = PcapRunningList.begin();CaptureIter != PcapRunningList.end();)
			{
				if (*CaptureIter == CaptureDevice)
				{
					CaptureIter = PcapRunningList.erase(CaptureIter);
					if (CaptureIter == PcapRunningList.end())
						break;
				}
				else {
					++CaptureIter;
				}
			}
			PcapRunningList.shrink_to_fit();
			CaptureMutex.unlock();

		//Exit this capture thread.
			pcap_freecode(BPF_Code.get());
			pcap_close(DeviceHandle);
			return false;
		}
		else {
			Sleep(MONITOR_LOOP_INTERVAL_TIME);
			continue;
		}
	}

//Monitor terminated
	pcap_freecode(BPF_Code.get());
	pcap_close(DeviceHandle);
	PrintError(LOG_ERROR_SYSTEM, L"Capture module Monitor terminated", 0, nullptr, 0);
	return true;
}

//Handler of WinPcap/LibPcap loop function
void CaptureHandler(unsigned char *Param, const struct pcap_pkthdr *PacketHeader, const unsigned char *PacketData)
{
//Initialization
	auto ParamList = (PCAPTURE_HANDLER_PARAM)Param;
	memset(ParamList->Buffer, 0, ORIGINAL_PACKET_MAXSIZE + sizeof(uint16_t));

//PPP(Such as ADSL, a part of organization networks)
	if (ParamList->DeviceType == DLT_EN10MB && ((peth_hdr)PacketData)->Type == htons(OSI_L2_PPPS) && PacketHeader->caplen > sizeof(eth_hdr) + sizeof(ppp_hdr) || //PPP over Ethernet II
		ParamList->DeviceType == DLT_APPLE_IP_OVER_IEEE1394 && ((pieee_1394_hdr)PacketData)->Type == htons(OSI_L2_PPPS) && PacketHeader->caplen > sizeof(ieee_1394_hdr) + sizeof(ppp_hdr)) //PPP over Apple IEEE 1394
	{
		size_t HeaderLength = 0;
		if (ParamList->DeviceType == DLT_EN10MB)
			HeaderLength += sizeof(eth_hdr);
		else if (ParamList->DeviceType == DLT_APPLE_IP_OVER_IEEE1394)
			HeaderLength += sizeof(ieee_1394_hdr);

		auto InnerHeader = (pppp_hdr)(PacketData + HeaderLength);
		if (
		//IPv6 over PPP
			InnerHeader->Protocol == htons(PPP_IPV6) && Parameter.DNSTarget.IPv6.AddressData.Storage.ss_family > 0 && 
			PacketHeader->caplen > HeaderLength + sizeof(ppp_hdr) + sizeof(ipv6_hdr) || 
		//IPv4 over PPP
			InnerHeader->Protocol == htons(PPP_IPV4) && Parameter.DNSTarget.IPv4.AddressData.Storage.ss_family > 0 && 
			PacketHeader->caplen > HeaderLength + sizeof(ppp_hdr) + sizeof(ipv4_hdr))
		{
			HeaderLength += sizeof(ppp_hdr);
			memcpy_s(ParamList->Buffer, ORIGINAL_PACKET_MAXSIZE, PacketData + HeaderLength, PacketHeader->caplen - HeaderLength);
			CaptureNetworkLayer(ParamList->Buffer, PacketHeader->caplen - HeaderLength, ntohs(InnerHeader->Protocol));
		}
	}

//LAN, WLAN and IEEE 802.1X, some Mobile Communications Standard/MCS drives which disguise as a LAN
	else if (
	//IPv6 over Ethernet II
		(ParamList->DeviceType == DLT_EN10MB && (((peth_hdr)PacketData)->Type == htons(OSI_L2_IPV6) && Parameter.DNSTarget.IPv6.AddressData.Storage.ss_family > 0 && 
		PacketHeader->caplen > sizeof(eth_hdr) + sizeof(ipv6_hdr) || 
	//IPv4 over Ethernet II
		((peth_hdr)PacketData)->Type == htons(OSI_L2_IPV4) && Parameter.DNSTarget.IPv4.AddressData.Storage.ss_family > 0 && 
		PacketHeader->caplen > sizeof(eth_hdr) + sizeof(ipv4_hdr))) || 
	//IPv6 over Apple IEEE 1394
		(ParamList->DeviceType == DLT_APPLE_IP_OVER_IEEE1394 && ((((pieee_1394_hdr)PacketData)->Type == htons(OSI_L2_IPV6) && 
		Parameter.DNSTarget.IPv6.AddressData.Storage.ss_family > 0 && PacketHeader->caplen > sizeof(ieee_1394_hdr) + sizeof(ipv6_hdr) || 
	//IPv4 over Apple IEEE 1394
		((pieee_1394_hdr)PacketData)->Type == htons(OSI_L2_IPV4) && Parameter.DNSTarget.IPv4.AddressData.Storage.ss_family > 0 && 
		PacketHeader->caplen > sizeof(ipv4_hdr)))))
	{
		if (ParamList->DeviceType == DLT_EN10MB)
		{
			memcpy_s(ParamList->Buffer, ORIGINAL_PACKET_MAXSIZE, PacketData + sizeof(eth_hdr), PacketHeader->caplen - sizeof(eth_hdr));
			CaptureNetworkLayer(ParamList->Buffer, PacketHeader->caplen - sizeof(eth_hdr), ntohs(((peth_hdr)PacketData)->Type));
		}
		else if (ParamList->DeviceType == DLT_APPLE_IP_OVER_IEEE1394)
		{
			memcpy_s(ParamList->Buffer, ORIGINAL_PACKET_MAXSIZE, PacketData + sizeof(ieee_1394_hdr), PacketHeader->caplen - sizeof(ieee_1394_hdr));
			CaptureNetworkLayer(ParamList->Buffer, PacketHeader->caplen - sizeof(ieee_1394_hdr), ntohs(((pieee_1394_hdr)PacketData)->Type));
		}
	}

	return;
}

//Network Layer(Internet Protocol/IP) process
bool __fastcall CaptureNetworkLayer(const char *Buffer, const size_t Length, const uint16_t Protocol)
{
//Initialization
/* Old version(2015-06-24)
	std::shared_ptr<char> Buffer(new char[Length]());
	memset(Buffer.get(), 0, Length);
	memcpy_s(Buffer.get(), Length, Recv, Length);
*/
	PDNS_SERVER_DATA PacketSource = nullptr;

//IPv6
	if (Protocol == PPP_IPV6 || Protocol == OSI_L2_IPV6)
	{
		auto IPv6_Header = (pipv6_hdr)Buffer;

	//Validate IPv6 header length.
		if (ntohs(IPv6_Header->PayloadLength) > Length - sizeof(ipv6_hdr))
			return false;

	//Mark source of packet.
		if (memcmp(&IPv6_Header->Source, &Parameter.DNSTarget.IPv6.AddressData.IPv6.sin6_addr, sizeof(in6_addr)) == 0)
		{
			PacketSource = &Parameter.DNSTarget.IPv6;
		}
		else if (memcmp(&IPv6_Header->Source, &Parameter.DNSTarget.Alternate_IPv6.AddressData.IPv6.sin6_addr, sizeof(in6_addr)) == 0)
		{
			PacketSource = &Parameter.DNSTarget.Alternate_IPv6;
		}
		else if (Parameter.DNSTarget.IPv6_Multi != nullptr)
		{
			for (size_t Index = 0;Index < Parameter.DNSTarget.IPv6_Multi->size();++Index)
			{
				if (memcmp(&IPv6_Header->Source, &Parameter.DNSTarget.IPv6_Multi->at(Index).AddressData.IPv6.sin6_addr, sizeof(in6_addr)) == 0)
				{
					PacketSource = &Parameter.DNSTarget.IPv6_Multi->at(Index);
					break;
				}
			}

			if (PacketSource == nullptr)
				return false;
		}
		else {
			return false;
		}

	//Get Hop Limits from IPv6 DNS server.
	//ICMPv6 Protocol
		if (Parameter.ICMP_Speed > 0 && IPv6_Header->NextHeader == IPPROTO_ICMPV6 && ntohs(IPv6_Header->PayloadLength) >= sizeof(icmpv6_hdr))
		{
		//Validate ICMPv6 checksum.
			if (GetICMPv6Checksum((PUINT8)Buffer, ntohs(IPv6_Header->PayloadLength), IPv6_Header->Destination, IPv6_Header->Source) != CHECKSUM_SUCCESS)
				return false;

		//ICMP check
			if (CaptureCheck_ICMP(Buffer + sizeof(ipv6_hdr), ntohs(IPv6_Header->PayloadLength), AF_INET6))
				PacketSource->HopLimitData.HopLimit = IPv6_Header->HopLimit;

			return true;
		}

	//TCP Protocol
		if (Parameter.HeaderCheck_TCP && IPv6_Header->NextHeader == IPPROTO_TCP && ntohs(IPv6_Header->PayloadLength) >= sizeof(tcp_hdr))
		{
		//Validate TCP checksum.
			if (GetTCPUDPChecksum((PUINT8)Buffer, ntohs(IPv6_Header->PayloadLength), AF_INET6, IPPROTO_TCP) != CHECKSUM_SUCCESS)
				return false;

		//TCP check
			if (CaptureCheck_TCP(Buffer + sizeof(ipv6_hdr)))
				PacketSource->HopLimitData.HopLimit = IPv6_Header->HopLimit;

			return true;
		}

	//UDP Protocol
		if (IPv6_Header->NextHeader == IPPROTO_UDP && ntohs(IPv6_Header->PayloadLength) >= sizeof(udp_hdr) + DNS_PACKET_MINSIZE)
		{
		//Validate UDP checksum.
			if (GetTCPUDPChecksum((PUINT8)Buffer, ntohs(IPv6_Header->PayloadLength), AF_INET6, IPPROTO_UDP) != CHECKSUM_SUCCESS)
				return false;

		//Port check
			auto UDP_Header = (pudp_hdr)(Buffer + sizeof(ipv6_hdr));
			if (UDP_Header->SrcPort == PacketSource->AddressData.IPv6.sin6_port)
			{
			//Domain Test and DNS Options check and get Hop Limit from Domain Test.
				switch (CheckResponseData(Buffer + sizeof(ipv6_hdr) + sizeof(udp_hdr), ntohs(IPv6_Header->PayloadLength) - sizeof(udp_hdr), false))
				{
					case EXIT_CHECK_RESPONSE_DATA_MARK_HOP_LIMITS:
					{
						PacketSource->HopLimitData.HopLimit = IPv6_Header->HopLimit;
					}break;
					case EXIT_FAILURE:
					{
						return false;
					}break;
				}

			//DNSCurve encryption packet check
			#if defined(ENABLE_LIBSODIUM)
				if (Parameter.DNSCurve && 
				//Main(IPv6)
					(DNSCurveParameter.DNSCurveTarget.IPv6.AddressData.Storage.ss_family > 0 && DNSCurveParameter.DNSCurveTarget.IPv6.ReceiveMagicNumber != nullptr && 
					memcmp(Buffer + sizeof(ipv6_hdr) + sizeof(udp_hdr), DNSCurveParameter.DNSCurveTarget.IPv6.ReceiveMagicNumber, DNSCURVE_MAGIC_QUERY_LEN) == 0 || 
				//Alternate(IPv6)
					DNSCurveParameter.DNSCurveTarget.Alternate_IPv6.AddressData.Storage.ss_family > 0 && DNSCurveParameter.DNSCurveTarget.Alternate_IPv6.ReceiveMagicNumber != nullptr && 
					memcmp(Buffer + sizeof(ipv6_hdr) + sizeof(udp_hdr), DNSCurveParameter.DNSCurveTarget.Alternate_IPv6.ReceiveMagicNumber, DNSCURVE_MAGIC_QUERY_LEN) == 0))
						return false;
			#endif

			//Hop Limit must not a ramdom value.
				if ((size_t)IPv6_Header->HopLimit + (size_t)Parameter.HopLimitFluctuation > (size_t)PacketSource->HopLimitData.HopLimit && 
					(size_t)IPv6_Header->HopLimit < (size_t)PacketSource->HopLimitData.HopLimit + (size_t)Parameter.HopLimitFluctuation)
				{
					MatchPortToSend(Buffer + sizeof(ipv6_hdr) + sizeof(udp_hdr), ntohs(IPv6_Header->PayloadLength) - sizeof(udp_hdr), AF_INET6, UDP_Header->DstPort);
					return true;
				}
			}
		}
	}
//IPv4
	else {
		auto IPv4_Header = (pipv4_hdr)Buffer;

	//Validate IPv4 header.
		if (ntohs(IPv4_Header->Length) <= IPv4_Header->IHL * IPv4_IHL_BYTES_TIMES || ntohs(IPv4_Header->Length) > Length || 
			GetChecksum((PUINT16)Buffer, sizeof(ipv4_hdr)) != CHECKSUM_SUCCESS) //Validate IPv4 header checksum.
				return false;

	//Mark source of packet.
		if (IPv4_Header->Source.s_addr == Parameter.DNSTarget.IPv4.AddressData.IPv4.sin_addr.s_addr)
		{
			PacketSource = &Parameter.DNSTarget.IPv4;
		}
		else if (IPv4_Header->Source.s_addr == Parameter.DNSTarget.Alternate_IPv4.AddressData.IPv4.sin_addr.s_addr)
		{
			PacketSource = &Parameter.DNSTarget.Alternate_IPv4;
		}
		else if (Parameter.DNSTarget.IPv4_Multi != nullptr)
		{
			for (size_t Index = 0;Index < Parameter.DNSTarget.IPv4_Multi->size();++Index)
			{
				if (IPv4_Header->Source.s_addr == Parameter.DNSTarget.IPv4_Multi->at(Index).AddressData.IPv4.sin_addr.s_addr)
				{
					PacketSource = &Parameter.DNSTarget.IPv4_Multi->at(Index);
					break;
				}
			}

			if (PacketSource == nullptr)
				return false;
		}
		else {
			return false;
		}

	//IPv4 options check
		if (Parameter.HeaderCheck_IPv4)
		{
		//No standard header length and header ID check
			if (IPv4_Header->IHL > IPV4_STANDARD_IHL || IPv4_Header->ID == 0)
				PacketSource->HopLimitData.TTL = IPv4_Header->TTL;

		//TOS and Flags should not be set.
			if (IPv4_Header->TOS > 0 || IPv4_Header->Flags > 0)
				return false;
		}

	//Get TTL from IPv4 DNS server.
	//ICMP Protocol
		if (Parameter.ICMP_Speed > 0 && IPv4_Header->Protocol == IPPROTO_ICMP && ntohs(IPv4_Header->Length) >= IPv4_Header->IHL * IPv4_IHL_BYTES_TIMES + sizeof(icmp_hdr))
		{
		//Validate ICMP checksum.
			if (GetChecksum((PUINT16)(Buffer + IPv4_Header->IHL * IPv4_IHL_BYTES_TIMES), ntohs(IPv4_Header->Length) - IPv4_Header->IHL * IPv4_IHL_BYTES_TIMES) != CHECKSUM_SUCCESS)
				return false;

		//ICMP Check
			if (CaptureCheck_ICMP(Buffer + IPv4_Header->IHL * IPv4_IHL_BYTES_TIMES, ntohs(IPv4_Header->Length) - IPv4_Header->IHL * IPv4_IHL_BYTES_TIMES, AF_INET))
				PacketSource->HopLimitData.TTL = IPv4_Header->TTL;

			return true;
		}

	//TCP Protocol
		if (Parameter.HeaderCheck_TCP && IPv4_Header->Protocol == IPPROTO_TCP && ntohs(IPv4_Header->Length) >= IPv4_Header->IHL * IPv4_IHL_BYTES_TIMES + sizeof(tcp_hdr))
		{
		//Validate TCP checksum.
			if (GetTCPUDPChecksum((PUINT8)Buffer, ntohs(IPv4_Header->Length) - IPv4_Header->IHL * IPv4_IHL_BYTES_TIMES, AF_INET, IPPROTO_TCP) != CHECKSUM_SUCCESS)
				return false;

		//TCP check
			if (CaptureCheck_TCP(Buffer + IPv4_Header->IHL * IPv4_IHL_BYTES_TIMES))
				PacketSource->HopLimitData.TTL = IPv4_Header->TTL;

			return true;
		}

	//UDP Protocol
		if (IPv4_Header->Protocol == IPPROTO_UDP && ntohs(IPv4_Header->Length) >= IPv4_Header->IHL * IPv4_IHL_BYTES_TIMES + sizeof(udp_hdr) + DNS_PACKET_MINSIZE)
		{
		//Validate UDP checksum.
			if (GetTCPUDPChecksum((PUINT8)Buffer, ntohs(IPv4_Header->Length) - IPv4_Header->IHL * IPv4_IHL_BYTES_TIMES, AF_INET, IPPROTO_UDP) != CHECKSUM_SUCCESS)
				return false;

		//Port check
			auto UDP_Header = (pudp_hdr)(Buffer + IPv4_Header->IHL * IPv4_IHL_BYTES_TIMES);
			if (UDP_Header->SrcPort == PacketSource->AddressData.IPv4.sin_port)
			{
			//Domain Test and DNS Options check and get TTL from Domain Test.
				switch (CheckResponseData(Buffer + IPv4_Header->IHL * IPv4_IHL_BYTES_TIMES + sizeof(udp_hdr), ntohs(IPv4_Header->Length) - IPv4_Header->IHL * IPv4_IHL_BYTES_TIMES - sizeof(udp_hdr), false))
				{
					case EXIT_CHECK_RESPONSE_DATA_MARK_HOP_LIMITS:
					{
						PacketSource->HopLimitData.TTL = IPv4_Header->TTL;
					}break;
					case EXIT_FAILURE:
					{
						return false;
					}break;
				}

			//DNSCurve encryption packet check
			#if defined(ENABLE_LIBSODIUM)
				if (Parameter.DNSCurve && 
				//Main(IPv4)
					(DNSCurveParameter.DNSCurveTarget.IPv4.AddressData.Storage.ss_family > 0 && DNSCurveParameter.DNSCurveTarget.IPv4.ReceiveMagicNumber != nullptr &&  
					memcmp(Buffer + IPv4_Header->IHL * IPv4_IHL_BYTES_TIMES + sizeof(udp_hdr), DNSCurveParameter.DNSCurveTarget.IPv4.ReceiveMagicNumber, DNSCURVE_MAGIC_QUERY_LEN) == 0 || 
				//Alternate(IPv4)
					DNSCurveParameter.DNSCurveTarget.Alternate_IPv4.AddressData.Storage.ss_family > 0 && DNSCurveParameter.DNSCurveTarget.Alternate_IPv4.ReceiveMagicNumber != nullptr && 
					memcmp(Buffer + IPv4_Header->IHL * IPv4_IHL_BYTES_TIMES + sizeof(udp_hdr), DNSCurveParameter.DNSCurveTarget.Alternate_IPv4.ReceiveMagicNumber, DNSCURVE_MAGIC_QUERY_LEN) == 0))
						return false;
			#endif

			//TTL must not a ramdom value.
				if ((size_t)IPv4_Header->TTL + (size_t)Parameter.HopLimitFluctuation > (size_t)PacketSource->HopLimitData.TTL && 
					(size_t)IPv4_Header->TTL < (size_t)PacketSource->HopLimitData.TTL + (size_t)Parameter.HopLimitFluctuation)
				{
					MatchPortToSend(Buffer + IPv4_Header->IHL * IPv4_IHL_BYTES_TIMES + sizeof(udp_hdr), ntohs(IPv4_Header->Length) - IPv4_Header->IHL * IPv4_IHL_BYTES_TIMES - sizeof(udp_hdr), AF_INET, UDP_Header->DstPort);
					return true;
				}
			}
		}
	}

	return true;
}

//ICMP header options check
bool __fastcall CaptureCheck_ICMP(const char *Buffer, const size_t Length, const uint16_t Protocol)
{
//ICMPv6
	if (Protocol == AF_INET6)
	{
		auto ICMPv6_Header = (picmpv6_hdr)Buffer;
		if (ICMPv6_Header->Type == ICMPV6_TYPE_REPLY && ICMPv6_Header->Code == ICMPV6_CODE_REPLY && //ICMPv6 Echo reply
			ICMPv6_Header->ID == Parameter.ICMP_ID) //Validate ICMP packet.
				return true;
	}
//ICMP
	else {
		auto ICMP_Header = (picmp_hdr)Buffer;
		if (ICMP_Header->Type == ICMP_TYPE_ECHO && ICMP_Header->Code == ICMP_CODE_ECHO && //ICMP Echo reply
		//Validate ICMP packet
			ICMP_Header->ID == Parameter.ICMP_ID && 
			Parameter.ICMP_PaddingData != nullptr && Length == sizeof(icmp_hdr) + Parameter.ICMP_PaddingLength - 1U && 
			memcmp(Parameter.ICMP_PaddingData, (PSTR)ICMP_Header + sizeof(icmp_hdr), Parameter.ICMP_PaddingLength - 1U) == 0) //Validate ICMP additional data.
				return true;
	}

	return false;
}

//TCP header options check
bool __fastcall CaptureCheck_TCP(const char *Buffer)
{
	auto TCP_Header = (ptcp_hdr)Buffer;
	if (TCP_Header->Acknowledge == 0 && TCP_Header->StatusFlags.Flags == TCP_RST_STATUS && TCP_Header->Windows == 0 || //RST bit is set, ACK bit shoule be set and Window size should be 0.
		TCP_Header->HeaderLength > TCP_STANDARDHL && TCP_Header->StatusFlags.Flags == TCP_SYN_ACK_STATUS) //TCP option usually should not empty(MSS, SACK_PERM and WS) whose Flags are 0x012(SYN bit and ACK bit is set).
			return true;

	return false;
}

//Match port of responses and send responses to system sockets process
bool __fastcall MatchPortToSend(const char *Buffer, const size_t Length, const uint16_t Protocol, const uint16_t Port)
{
//Initialization
	std::shared_ptr<SOCKET_DATA> SocketData_Input(new SOCKET_DATA());
	memset(SocketData_Input.get(), 0, sizeof(SOCKET_DATA));
	uint16_t SystemProtocol = 0;
	size_t ReceiveIndex = 0;

//Match port.
	std::unique_lock<std::mutex> OutputPacketListMutex(OutputPacketListLock);
	for (auto &PortTableIter:OutputPacketList)
	{
		for (auto &SocketDataIter:PortTableIter.SocketData_Output)
		{
			if (PortTableIter.ClearPortTime > 0 && //Do not scan timeout data.
				Protocol == AF_INET6 && SocketDataIter.AddrLen == sizeof(sockaddr_in6) && SocketDataIter.SockAddr.ss_family == AF_INET6 && 
				Port == ((PSOCKADDR_IN6)&SocketDataIter.SockAddr)->sin6_port || //IPv6
				Protocol == AF_INET && SocketDataIter.AddrLen == sizeof(sockaddr_in) && SocketDataIter.SockAddr.ss_family == AF_INET && 
				Port == ((PSOCKADDR_IN)&SocketDataIter.SockAddr)->sin_port) //IPv4
			{
				if (Parameter.ReceiveWaiting > 0)
				{
					++PortTableIter.ReceiveIndex;
					ReceiveIndex = PortTableIter.ReceiveIndex;

					OutputPacketListLock.unlock();
					goto StopLoop;
				}
				else {
					*SocketData_Input = PortTableIter.SocketData_Input;
					SystemProtocol = PortTableIter.Protocol_Network;
					PortTableIter.ClearPortTime = 0;

					goto ClearOutputPacketListData;
				}
			}
		}
	}

	goto ClearOutputPacketListData;

//Stop loop, wait receiving and match port again.
	StopLoop: 
	Sleep(Parameter.ReceiveWaiting);
	OutputPacketListLock.lock();
	for (auto &PortTableIter:OutputPacketList)
	{
		for (auto &SocketDataIter:PortTableIter.SocketData_Output)
		{
			if (PortTableIter.ClearPortTime > 0 && //Do not scan timeout data.
				Protocol == AF_INET6 && SocketDataIter.AddrLen == sizeof(sockaddr_in6) && SocketDataIter.SockAddr.ss_family == AF_INET6 && 
				Port == ((PSOCKADDR_IN6)&SocketDataIter.SockAddr)->sin6_port || //IPv6
				Protocol == AF_INET && SocketDataIter.AddrLen == sizeof(sockaddr_in) && SocketDataIter.SockAddr.ss_family == AF_INET && 
				Port == ((PSOCKADDR_IN)&SocketDataIter.SockAddr)->sin_port) //IPv4
			{
				if (PortTableIter.ReceiveIndex == ReceiveIndex)
				{
					*SocketData_Input = PortTableIter.SocketData_Input;
					SystemProtocol = PortTableIter.Protocol_Network;
					PortTableIter.ClearPortTime = 0;
				}
				else {
					return false;
				}

				goto ClearOutputPacketListData;
			}
		}
	}

//Stop loop and clear timeout data.
	ClearOutputPacketListData: 
//Minimum supported system of GetTickCount64() is Windows Vista(Windows XP with SP3 support).
#if (defined(PLATFORM_WIN32) && !defined(PLATFORM_WIN64))
	if (Parameter.GetTickCount64_PTR != nullptr)
	{
		while (!OutputPacketList.empty() && OutputPacketList.front().ClearPortTime <= (size_t)((*Parameter.GetTickCount64_PTR)()))
		{
		//Mark timeout.
			if (OutputPacketList.front().ClearPortTime > 0)
			{
				if (OutputPacketList.front().Protocol_Network == AF_INET6) //IPv6
				{
					if (OutputPacketList.front().Protocol_Transport == IPPROTO_TCP) //TCP
						++AlternateSwapList.TimeoutTimes[0];
					else //UDP
						++AlternateSwapList.TimeoutTimes[2U];
				}
				else if (OutputPacketList.front().Protocol_Network == AF_INET) //IPv4
				{
					if (OutputPacketList.front().Protocol_Transport == IPPROTO_TCP) //TCP
						++AlternateSwapList.TimeoutTimes[1U];
					else //UDP
						++AlternateSwapList.TimeoutTimes[3U];
				}
			}

			OutputPacketList.pop_front();
		}
	}
	else {
		while (!OutputPacketList.empty() && OutputPacketList.front().ClearPortTime <= GetTickCount())
		{
		//Mark timeout.
			if (OutputPacketList.front().ClearPortTime > 0)
			{
				if (OutputPacketList.front().Protocol_Network == AF_INET6) //IPv6
				{
					if (OutputPacketList.front().Protocol_Transport == IPPROTO_TCP) //TCP
						++AlternateSwapList.TimeoutTimes[0];
					else //UDP
						++AlternateSwapList.TimeoutTimes[2U];
				}
				else if (OutputPacketList.front().Protocol_Network == AF_INET) //IPv4
				{
					if (OutputPacketList.front().Protocol_Transport == IPPROTO_TCP) //TCP
						++AlternateSwapList.TimeoutTimes[1U];
					else //UDP
						++AlternateSwapList.TimeoutTimes[3U];
				}
			}

			OutputPacketList.pop_front();
		}
	}
#else
	while (!OutputPacketList.empty() && OutputPacketList.front().ClearPortTime <= GetTickCount64())
	{
	//Mark timeout.
		if (OutputPacketList.front().ClearPortTime > 0)
		{
			if (OutputPacketList.front().Protocol_Network == AF_INET6) //IPv6
			{
				if (OutputPacketList.front().Protocol_Transport == IPPROTO_TCP) //TCP
					++AlternateSwapList.TimeoutTimes[0];
				else //UDP
					++AlternateSwapList.TimeoutTimes[2U];
			}
			else if (OutputPacketList.front().Protocol_Network == AF_INET) //IPv4
			{
				if (OutputPacketList.front().Protocol_Transport == IPPROTO_TCP) //TCP
					++AlternateSwapList.TimeoutTimes[1U];
				else //UDP
					++AlternateSwapList.TimeoutTimes[3U];
			}
		}

		OutputPacketList.pop_front();
	}
#endif
	OutputPacketListMutex.unlock();

//Drop resopnses which not in OutputPacketList.
	if (SocketData_Input->Socket == 0 || SocketData_Input->AddrLen == 0 || SocketData_Input->SockAddr.ss_family == 0 || SystemProtocol == 0)
		return false;

//Mark DNS Cache.
	if (Parameter.CacheType > 0)
		MarkDomainCache(Buffer, Length);

//Send to localhost.
	SendToRequester((PSTR)Buffer, Length, SystemProtocol, *SocketData_Input);
	if (SystemProtocol == IPPROTO_TCP)
		return true;
/* Old version(2015-06-24)
		std::shared_ptr<char> RecvBuffer(new char[Length + sizeof(uint16_t)]());
		memset(RecvBuffer.get(), 0, Length + sizeof(uint16_t));
		memcpy_s(RecvBuffer.get(), Length + sizeof(uint16_t), Buffer, Length);
		if (AddLengthDataToDNSHeader(RecvBuffer.get(), Length, Length + sizeof(uint16_t)) == EXIT_FAILURE)
			return false;

		if (AddLengthDataToDNSHeader((PSTR)Buffer, Length, Length + sizeof(uint16_t)) == EXIT_FAILURE)
			return false;

		send(SocketData_Input->Socket, Buffer, (int)(Length + sizeof(uint16_t)), 0);
		shutdown(SocketData_Input->Socket, SD_BOTH);
		closesocket(SocketData_Input->Socket);
		return true;
	}
	else { //UDP
		sendto(SocketData_Input->Socket, Buffer, (int)Length, 0, (PSOCKADDR)&SocketData_Input->SockAddr, SocketData_Input->AddrLen);
	}
*/

//Check global sockets.
	if (Parameter.LocalSocket != nullptr && !Parameter.LocalSocket->empty())
	{
		for (auto SocketIter:*Parameter.LocalSocket)
		{
			if (SocketIter == SocketData_Input->Socket)
				return true;
		}
	}

	shutdown(SocketData_Input->Socket, SD_BOTH);
	closesocket(SocketData_Input->Socket);
	return true;
}
#endif