
/*
Based on UnitySample.cpp from NatNet_SDK Sample Unity3D

This program connects to a NatNet server, receives a data stream, encodes the rigid objects and markers to XML,
and outputs XML locally over UDP.

*/

#include <stdio.h>
#include <tchar.h>
#include <conio.h>
#include <winsock2.h>
#include <string>
#include <sstream>
#include <map>

#include "NatNetTypes.h"
#include "NatNetClient.h"

#include "tinyxml/tinyxml.h"  //== for xml encoding
#include "NatNetRepeater.h"   //== for transport of data over UDP 

//== Slip Stream globals ==--

cSlipStream gSlipStream("127.0.0.1", 16000);
std::map<int, std::string> gBoneNames;

#pragma warning( disable : 4996 )

void __cdecl DataHandler(sFrameOfMocapData* data, void* pUserData);     // receives data from the server
void __cdecl MessageHandler(int msgType, char* msg);                    // receives NatNet error mesages
void resetClient();
int CreateClient(int iConnectionType);

unsigned int MyServersDataPort = 3130;
unsigned int MyServersCommandPort = 3131;

NatNetClient* theClient;
FILE* fp;

char szMyIPAddress[128] = "";
char szServerIPAddress[128] = "";


int _tmain(int argc, _TCHAR* argv[])
{
	int iResult;
	int iConnectionType = ConnectionType_Multicast;
	//int iConnectionType = ConnectionType_Unicast;

	// parse command line args
	if (argc>1)
	{
		strcpy(szServerIPAddress, argv[1]); // specified on command line
		printf("Connecting to server at %s...\n", szServerIPAddress);
	}
	else
	{
		strcpy(szServerIPAddress, "");      // not specified - assume server is local machine
		printf("Connecting to server at LocalMachine\n");
	}
	if (argc>2)
	{
		strcpy(szMyIPAddress, argv[2]);     // specified on command line
		printf("Connecting from %s...\n", szMyIPAddress);
	}
	else
	{
		strcpy(szMyIPAddress, "");          // not specified - assume server is local machine
		printf("Connecting from LocalMachine...\n");
	}

	// Create NatNet Client
	iResult = CreateClient(iConnectionType);
	if (iResult != ErrorCode_OK)
	{
		printf("Error initializing client.  See log for details.  Exiting");
		return 1;
	}
	else
	{
		printf("Client initialized and ready.\n");
	}


	// send/receive test request
	printf("[SampleClient] Sending Test Request\n");
	void* response;
	int nBytes;
	iResult = theClient->SendMessageAndWait("TestRequest", &response, &nBytes);
	if (iResult == ErrorCode_OK)
	{
		printf("[SampleClient] Received: %s", (char*)response);
	}

		// Ready to receive marker stream!
	printf("\nClient is connected to server and listening for data...\n");
	int c;
	bool bExit = false;
	while (c = _getch())
	{
		switch (c)
		{
		case 'q':
			bExit = true;
			break;
		case 'r':
			resetClient();
			break;
		case 'p':
			sServerDescription ServerDescription;
			memset(&ServerDescription, 0, sizeof(ServerDescription));
			theClient->GetServerDescription(&ServerDescription);
			if (!ServerDescription.HostPresent)
			{
				printf("Unable to connect to server. Host not present. Exiting.");
				return 1;
			}
			break;
		case 'f':
		{
			sFrameOfMocapData* pData = theClient->GetLastFrameOfData();
			printf("Most Recent Frame: %d", pData->iFrame);
		}
		break;
		case 'm':                           // change to multicast
			iResult = CreateClient(ConnectionType_Multicast);
			if (iResult == ErrorCode_OK)
				printf("Client connection type changed to Multicast.\n\n");
			else
				printf("Error changing client connection type to Multicast.\n\n");
			break;
		case 'u':                           // change to unicast
			iResult = CreateClient(ConnectionType_Unicast);
			if (iResult == ErrorCode_OK)
				printf("Client connection type changed to Unicast.\n\n");
			else
				printf("Error changing client connection type to Unicast.\n\n");
			break;


		default:
			break;
		}
		if (bExit)
			break;
	}

	// Done - clean up.
	theClient->Uninitialize();

	return ErrorCode_OK;
}

// Establish a NatNet Client connection
int CreateClient(int iConnectionType)
{
	// release previous server
	if (theClient)
	{
		theClient->Uninitialize();
		delete theClient;
	}

	// create NatNet client
	theClient = new NatNetClient(iConnectionType);

	// [optional] use old multicast group
	//theClient->SetMulticastAddress("224.0.0.1");

	// print version info
	unsigned char ver[4];
	theClient->NatNetVersion(ver);
	printf("NatNet Sample Client (NatNet ver. %d.%d.%d.%d)\n", ver[0], ver[1], ver[2], ver[3]);

	// Set callback handlers
	theClient->SetMessageCallback(MessageHandler);
	theClient->SetVerbosityLevel(Verbosity_Debug);
	theClient->SetDataCallback(DataHandler, theClient);   // this function will receive data from the server

														  // Init Client and connect to NatNet server
														  // to use NatNet default port assigments
	int retCode = theClient->Initialize(szMyIPAddress, szServerIPAddress);
	// to use a different port for commands and/or data:
	//int retCode = theClient->Initialize(szMyIPAddress, szServerIPAddress, MyServersCommandPort, MyServersDataPort);
	if (retCode != ErrorCode_OK)
	{
		printf("Unable to connect to server.  Error code: %d. Exiting", retCode);
		return ErrorCode_Internal;
	}
	else
	{
		// print server info
		sServerDescription ServerDescription;
		memset(&ServerDescription, 0, sizeof(ServerDescription));
		theClient->GetServerDescription(&ServerDescription);
		if (!ServerDescription.HostPresent)
		{
			printf("Unable to connect to server. Host not present. Exiting.");
			return 1;
		}
		printf("[SampleClient] Server application info:\n");
		printf("Application: %s (ver. %d.%d.%d.%d)\n", ServerDescription.szHostApp, ServerDescription.HostAppVersion[0],
			ServerDescription.HostAppVersion[1], ServerDescription.HostAppVersion[2], ServerDescription.HostAppVersion[3]);
		printf("NatNet Version: %d.%d.%d.%d\n", ServerDescription.NatNetVersion[0], ServerDescription.NatNetVersion[1],
			ServerDescription.NatNetVersion[2], ServerDescription.NatNetVersion[3]);
		printf("Client IP:%s\n", szMyIPAddress);
		printf("Server IP:%s\n", szServerIPAddress);
		printf("Server Name:%s\n\n", ServerDescription.szHostComputerName);
	}

	return ErrorCode_OK;

}

// Create XML from frame data and output to Unity
void SendFrameToUnity(sFrameOfMocapData *data, void *pUserData)
{
	// Get the data and add it in to the XML file
	if (data->nRigidBodies>0)
	{
		//create de document
		TiXmlDocument doc;
		TiXmlDeclaration* decl = new TiXmlDeclaration("1.0", "", "");
		doc.LinkEndChild(decl);
		
		//Add the root
		TiXmlElement *root = new TiXmlElement("RigidBodies");
		doc.LinkEndChild(root);

		TiXmlElement *body;

		int nRG = data->nRigidBodies;
	    //for each body add a new child element to root tagged body (this will make easy to get them on the client)
		for (int i = 0; i<nRG; i++)
		{
			sRigidBodyData tempBody = data->RigidBodies[i];
			body = new TiXmlElement("Body");
			root->LinkEndChild(body);
			//Add the atributs of the body
			body->SetAttribute("ID", tempBody.ID);
			body->SetDoubleAttribute("x", tempBody.x);
			body->SetDoubleAttribute("y", tempBody.y);
			body->SetDoubleAttribute("z", tempBody.z);
			body->SetDoubleAttribute("qx", tempBody.qx);
			body->SetDoubleAttribute("qy", tempBody.qy);
			body->SetDoubleAttribute("qz", tempBody.qz);
			body->SetDoubleAttribute("qw", tempBody.qw);
			int rbnM = data->RigidBodies[i].nMarkers;
			body->SetAttribute("nMarkers", rbnM);
			//For each marker add a new child element to the body. 
			for (int j = 0; j < rbnM; j++)
			{
				std::stringstream ss;
				ss << int(tempBody.ID);
				std::string idB = ss.str().c_str();
				//Each marker is realated to the body it belongs (this way we get the all the markers of the body and not all the markers)
				TiXmlElement *marker = new TiXmlElement("Marker" + idB);
				body->LinkEndChild(marker);
				marker->SetAttribute("ID", data->RigidBodies[i].MarkerIDs[j]);
				marker->SetDoubleAttribute("x", data->RigidBodies[i].Markers[j][0]);
				marker->SetDoubleAttribute("y", data->RigidBodies[i].Markers[j][1]);
				marker->SetDoubleAttribute("z", data->RigidBodies[i].Markers[j][2]);
			}
		}

		// convert xml document into a buffer filled with data ==--
		std::ostringstream stream;
		stream << doc;
		std::string str = stream.str();
		const char* buffer = str.c_str();

		// stream xml data over UDP via SlipStream ==--

		gSlipStream.Stream((unsigned char *)buffer, int(strlen(buffer)));
		
	}
	else
	{
		;
	}
}

// DataHandler receives data from the server
void __cdecl DataHandler(sFrameOfMocapData* data, void* pUserData)
{
	NatNetClient* pClient = (NatNetClient*)pUserData;
	
	printf("Received frame %d\n", data->iFrame);
	//printf("Rigid Bodies %d\n", data->nRigidBodies);
	SendFrameToUnity(data, pUserData);
	//printf("Frame enviado");
}

// MessageHandler receives NatNet error/debug messages
void __cdecl MessageHandler(int msgType, char* msg)
{
	printf("\n%s\n", msg);
}

void resetClient()
{
	int iSuccess;

	printf("\n\nre-setting Client\n\n.");

	iSuccess = theClient->Uninitialize();
	if (iSuccess != 0)
		printf("error un-initting Client\n");

	iSuccess = theClient->Initialize(szMyIPAddress, szServerIPAddress);
	if (iSuccess != 0)
		printf("error re-initting Client\n");


}

