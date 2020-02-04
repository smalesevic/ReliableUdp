/*
	Reliability and Flow Control Example
	From "Networking for Game Programmers" - http://www.gaffer.org/networking-for-game-programmers
	Author: Glenn Fiedler <gaffer@gaffer.org>
*/

#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <list>

#include "Net.h"

#pragma warning(disable:4996)

//#define SHOW_ACKS

using namespace std;
using namespace net;

const int ServerPort = 30001;
const int ClientPort = 30000;
const int ProtocolId = 0x11223344;
const float DeltaTime = 1.0f / 30.0f;
const float SendRate = 1.0f / 30.0f;
const float TimeOut = 10.0f;
const int PacketSize = 256;

class FlowControl
{
public:

	FlowControl()
	{
		printf("flow control initialized\n");
		Reset();
	}

	void Reset()
	{
		mode = Bad;
		penalty_time = 4.0f;
		good_conditions_time = 0.0f;
		penalty_reduction_accumulator = 0.0f;
	}

	void Update(float deltaTime, float rtt)
	{
		const float RTT_Threshold = 250.0f;

		if (mode == Good)
		{
			if (rtt > RTT_Threshold)
			{
				printf("*** dropping to bad mode ***\n");
				mode = Bad;
				if (good_conditions_time < 10.0f && penalty_time < 60.0f)
				{
					penalty_time *= 2.0f;
					if (penalty_time > 60.0f)
						penalty_time = 60.0f;
					printf("penalty time increased to %.1f\n", penalty_time);
				}
				good_conditions_time = 0.0f;
				penalty_reduction_accumulator = 0.0f;
				return;
			}

			good_conditions_time += deltaTime;
			penalty_reduction_accumulator += deltaTime;

			if (penalty_reduction_accumulator > 10.0f && penalty_time > 1.0f)
			{
				penalty_time /= 2.0f;
				if (penalty_time < 1.0f)
					penalty_time = 1.0f;
				printf("penalty time reduced to %.1f\n", penalty_time);
				penalty_reduction_accumulator = 0.0f;
			}
		}

		if (mode == Bad)
		{
			if (rtt <= RTT_Threshold)
				good_conditions_time += deltaTime;
			else
				good_conditions_time = 0.0f;

			if (good_conditions_time > penalty_time)
			{
				printf("*** upgrading to good mode ***\n");
				good_conditions_time = 0.0f;
				penalty_reduction_accumulator = 0.0f;
				mode = Good;
				return;
			}
		}
	}

	float GetSendRate()
	{
		return mode == Good ? 30.0f : 10.0f;
	}

private:

	enum Mode
	{
		Good,
		Bad
	};

	Mode mode;
	float penalty_time;
	float good_conditions_time;
	float penalty_reduction_accumulator;
};

// ----------------------------------------------

int main(int argc, char* argv[])
{
	// parse command line

	enum Mode
	{
		Client,
		Server
	};

	Mode mode = Server;
	Address address;
	unsigned char fName[FILE_NAME_SIZE] = "";

	struct PacketData
	{
		unsigned char packet[PacketSize];
		bool firstPacket = false;
	};

	PacketData firstPacket;

	vector<PacketData>allPackets;

	if (argc >= 2)
	{
		int a, b, c, d;
		if (sscanf_s(argv[1], "%d.%d.%d.%d", &a, &b, &c, &d))	//changed 'sscanf' to 'sscanf_s'
		{
			mode = Client;
			address = Address(a, b, c, d, ServerPort);
		}
		if (sscanf_s(argv[2], "%s", &fName))	//changed 'sscanf' to 'sscanf_s'
		{
			char bytesName = sizeof(fName);

			// Header contains [packetID][numBytesToRead] 
			unsigned char header[2] = { '0', bytesName };

			// Add header 
			memcpy(firstPacket.packet, header, 2);

			// Add first packet to the list of packets
			memcpy(firstPacket.packet + 2, fName, sizeof(fName));
			allPackets.push_back(firstPacket);
		}
	}

		// initialize

	if (!InitializeSockets())
	{
		printf("failed to initialize sockets\n");
		return 1;
	}

	ReliableConnection connection(ProtocolId, TimeOut);

	const int port = mode == Server ? ServerPort : ClientPort;

	if (!connection.Start(port))
	{
		printf("could not start connection on port %d\n", port);
		return 1;
	}

	if (mode == Client)
		connection.Connect(address);
	else
		connection.Listen();

	bool connected = false;
	float sendAccumulator = 0.0f;
	float statsAccumulator = 0.0f;

	FlowControl flowControl;

	while (true)
	{
		// update flow control

		if (connection.IsConnected())
			flowControl.Update(DeltaTime, connection.GetReliabilitySystem().GetRoundTripTime() * 1000.0f);

		const float sendRate = flowControl.GetSendRate();

		// detect changes in connection state

		if (mode == Server && connected && !connection.IsConnected())
		{
			flowControl.Reset();
			printf("reset flow control\n");
			connected = false;
		}

		if (!connected && connection.IsConnected())
		{
			printf("client connected to server\n");
			connected = true;
		}

		if (!connected && connection.ConnectFailed())
		{
			printf("connection failed\n");
			break;
		}

		// send and receive packets

		sendAccumulator += DeltaTime;

		while (sendAccumulator > 1.0f / sendRate)
		{
			unsigned char packet[PacketSize] = "Hello World";	//hard-coded Hello World into packet
			if (mode == Client)
			{
				// Send file name as first 50 characters of packet each time 
				// Only if file name was specified 

				if (firstPacket.firstPacket != true)
				{
					memcpy(packet, allPackets.front().packet, sizeof(allPackets.front().packet));
				}
				/*
				if (strcmp(fName, "") != 0)
				{
					// Add file name to start of packet
					memcpy(packet, fName , sizeof(fName));

					FILE* fp; //file pointer
					char fileBuffer[PacketSize];
					fp = fopen(fName, "rb");

					// Read file contents
					fread(fileBuffer, 1, sizeof(fileBuffer), fp);

					// Copy file contents to packet, offset by filename size
					memcpy(packet + FILE_NAME_SIZE, fileBuffer, sizeof(fileBuffer));

					fclose(fp);
				}
				*/
				//memset(packet, 0, sizeof(packet));				//commented out memset to replace with our own
			}
			connection.SendPacket(packet, sizeof(packet));
			
			while (true)
			{
				char lastReceived;
				char bytesToRead;
				unsigned char packet[256];
				int bytes_read = connection.ReceivePacket(packet, sizeof(packet));
				if (bytes_read == 0)
					break;

	
				lastReceived = packet[0];
				int lastPID = lastReceived - 48;
				if (lastPID == 0)
				{
					firstPacket.firstPacket = true;
				}
				
			}
			sendAccumulator -= 1.0f / sendRate;
		}

		char lastReceived; 
		char bytesToRead;
		while (true)
		{
			unsigned char packet[256];
			int bytes_read = connection.ReceivePacket(packet, sizeof(packet));
			if (bytes_read == 0)
				break;

			if (mode == Server)
			{
				lastReceived = packet[0];
				bytesToRead = packet[1];
				printf("%s", packet);

			
				if (lastReceived == '0')
				{
					firstPacket.firstPacket = true;
					unsigned char packet[PacketSize] = "0";
					connection.SendPacket(packet, sizeof(packet));
				}
			}
			else
			{
				lastReceived = packet[0];
				int lastPID = lastReceived - 48;
				if (lastPID == 0)
				{
					firstPacket.firstPacket = true;
				}
			}
		}

		// show packets that were acked this frame

#ifdef SHOW_ACKS
		unsigned int* acks = NULL;
		int ack_count = 0;
		connection.GetReliabilitySystem().GetAcks(&acks, ack_count);
		if (ack_count > 0)
		{
			printf("acks: %d", acks[0]);
			for (int i = 1; i < ack_count; ++i)
				printf(",%d", acks[i]);
			printf("\n");
		}
#endif

		// update connection

		connection.Update(DeltaTime);

		// show connection stats

		statsAccumulator += DeltaTime;

		/*
		while (statsAccumulator >= 0.25f && connection.IsConnected())
		{
			float rtt = connection.GetReliabilitySystem().GetRoundTripTime();

			unsigned int sent_packets = connection.GetReliabilitySystem().GetSentPackets();
			unsigned int acked_packets = connection.GetReliabilitySystem().GetAckedPackets();
			unsigned int lost_packets = connection.GetReliabilitySystem().GetLostPackets();

			float sent_bandwidth = connection.GetReliabilitySystem().GetSentBandwidth();
			float acked_bandwidth = connection.GetReliabilitySystem().GetAckedBandwidth();

			printf("rtt %.1fms, sent %d, acked %d, lost %d (%.1f%%), sent bandwidth = %.1fkbps, acked bandwidth = %.1fkbps\n",
				rtt * 1000.0f, sent_packets, acked_packets, lost_packets,
				sent_packets > 0.0f ? (float)lost_packets / (float)sent_packets * 100.0f : 0.0f,
				sent_bandwidth, acked_bandwidth);

			statsAccumulator -= 0.25f;
		}
		*/
		net::wait(DeltaTime);
	}

	ShutdownSockets();

	return 0;
}
