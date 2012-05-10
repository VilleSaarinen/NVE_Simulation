/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation;
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */


#include "ns3/applications-module.h"
#include "ns3/nstime.h"
#include "ns3/core-module.h"
#include "ns3/point-to-point-helper.h"
#include "ns3/internet-stack-helper.h"
#include "ns3/ipv4-address-helper.h"
#include "ns3/ipv4-interface-container.h"
#include "ns3/ipv4-global-routing-helper.h"
#include "ns3/inet-socket-address.h"
#include "ns3/drop-tail-queue.h"
#include "XML_parser.h"
#include "Server.h"
#include "StatisticsCollector.h"
#include "Client.h"
#include "utilities.h"
#include <cstdlib>
#include <fstream>
#include <sstream>


void printAddresses(NetDeviceContainer *deviceContainer, Ipv4InterfaceContainer *ipv4Container,  int count);
void printHelpAndQuit();

int main(int argc, char** argv){

    int runningTime = 20; //TODO: change to be configurable

    int i;
    std::stringstream str;
    std::string addressBase;
    uint16_t numberOfClients;
    uint16_t totalNumberOfNodes;
    Ipv4InterfaceContainer routerServerIpInterfaces;
    Ipv4AddressHelper address;
    NodeContainer allNodes;
    StatisticsCollector* stats;
    uint16_t* serverPorts;
    bool verbose = false, clientLog = false, serverLog = false;
    bool fileNameGiven = false;
    std::string XML_filename;

    DropTailQueue::GetTypeId();

    Config::SetDefault("ns3::DropTailQueue::MaxPackets", UintegerValue(2000));    //TODO: change to be configurable

    for(i = 0; i < argc; i++){
        if(strcmp(argv[i], "--verbose") == 0){
            verbose = clientLog = serverLog = true;
        }
        if(strcmp(argv[i], "--serverLog") == 0){
            serverLog = true;
        }
        if(strcmp(argv[i], "--clientLog") == 0){
            clientLog = true;
        }
        if(strcmp(argv[i], "--filename") == 0){
            if(++i >= argc){
                PRINT_ERROR( "No filename given" << std::endl);
                printHelpAndQuit();
            }else{
                XML_filename = std::string(argv[i]);
                fileNameGiven = true;
            }
        }
        if(strcmp(argv[i], "--help") == 0){
            printHelpAndQuit();
        }
    }

    if(!fileNameGiven){
        PRINT_ERROR( "No filename given." << std::endl);
        printHelpAndQuit();
    }

    stats = StatisticsCollector::createStatisticsCollector(verbose, clientLog, serverLog);

    if(stats == NULL){
        PRINT_ERROR( "Can't create statistics collector!" << std::endl);
        return EXIT_FAILURE;
    }


    XMLParser parser = XMLParser(XML_filename);

    if(!parser.isFileCorrect()){
        PRINT_ERROR( "Terminating due to an incorrect XML file" << std::endl);
        delete stats;
        return EXIT_FAILURE;
    }

    numberOfClients = parser.getNumberOfClients();
    totalNumberOfNodes = numberOfClients + 2;

    serverPorts = new uint16_t[parser.getNumberOfStreams()];

    allNodes.Create(totalNumberOfNodes);   //a node for each client, one for the router and one for the server

    NodeContainer clientRouterNodes[numberOfClients];

    for(int i = 0; i < numberOfClients; i++){
        clientRouterNodes[i] = NodeContainer(allNodes.Get(i), allNodes.Get(numberOfClients));
    }

    NodeContainer routerServerNodes = NodeContainer(allNodes.Get(numberOfClients), allNodes.Get(numberOfClients+1));

    PointToPointHelper pointToPoint[numberOfClients + 1];    //point-to-point connection for each client-router connection and one for router-server connection

    NetDeviceContainer clientRouterDevices[numberOfClients];

    for(i = 0; i < numberOfClients; i++){
        clientRouterDevices[i] = pointToPoint[i].Install(clientRouterNodes[i]);
    }

    NetDeviceContainer routerServerDevices = pointToPoint[numberOfClients].Install(routerServerNodes);

    InternetStackHelper stack;
    stack.Install(allNodes);

    str.str("");

    Ipv4InterfaceContainer clientRouterIpInterfaces[numberOfClients];

    for(i = 0; i < numberOfClients; i++, str.str("")){
        str << "10.1." << i+1 << ".0";
        addressBase = str.str();
        address.SetBase(addressBase.c_str(), "255.255.255.0", "0.0.0.1");
        clientRouterIpInterfaces[i] = address.Assign(clientRouterDevices[i]);
    }

    str << "10.1." << numberOfClients+1 << ".0";
    address.SetBase(str.str().c_str(), "255.255.255.0", "0.0.0.1");
    routerServerIpInterfaces = address.Assign(routerServerDevices);

    Address serverAddress(InetSocketAddress(routerServerIpInterfaces.GetAddress(1), 10000));

    Client* clients[numberOfClients];
    Server server = Server(parser, runningTime, routerServerNodes.Get(1), serverAddress);

    for(uint16_t i = 0; i < numberOfClients; i++){
        clients[i] = new Client(parser, i+1, runningTime, clientRouterNodes[i].Get(0), serverAddress);
        PRINT_INFO(*(clients[i]) << std::endl);
    }

    for(i = 0; i < numberOfClients; i++){
        //TODO: implement data rate configuration
        pointToPoint[i].SetChannelAttribute("Delay", StringValue(clients[i]->getDelayInMilliseconds()));
    }

    pointToPoint[numberOfClients].SetDeviceAttribute("DataRate", StringValue("1Gbps"));

    //TODO: add network configurations maybe here

    if(verbose){
        printAddresses(clientRouterDevices, clientRouterIpInterfaces, numberOfClients);
        printAddresses(&routerServerDevices, &routerServerIpInterfaces, 1);
    }

    Ipv4GlobalRoutingHelper::PopulateRoutingTables();

    for(i = 0; i < numberOfClients; i++){
        pointToPoint[i].EnablePcapAll("results/results.txt");
    }

    AsciiTraceHelper ascii;
    pointToPoint[numberOfClients].EnableAsciiAll(ascii.CreateFileStream ("results/nve.tr"));

    Simulator::Run();
    Simulator::Destroy();

    for(i = 0; i < numberOfClients; i++){
        if(clients[i] != 0)
            delete clients[i];
    }

    delete[] serverPorts;

    delete stats;

    return EXIT_SUCCESS;

}

void printAddresses(NetDeviceContainer *deviceContainer, Ipv4InterfaceContainer *ipv4Container, int count){

    NetDevice* device;
    Ipv4InterfaceAddress address;
    Ipv4* tempAddress;
    std::vector<Ptr<NetDevice> >::const_iterator macIt;
    std::vector<std::pair<Ptr<Ipv4>, uint32_t > >::const_iterator ipIt;

    for(int i = 0; i < count; i++){

        for(macIt =(deviceContainer +i)->Begin(), ipIt = (ipv4Container +i)->Begin(); (macIt != (deviceContainer +i)->End()) && (ipIt != (ipv4Container +i)->End()); macIt++, ipIt++){
            device = GetPointer(*macIt);
            tempAddress = GetPointer(ipIt->first);
            address = tempAddress->GetAddress(ipIt->second, 0);
            tempAddress->Unref();
            device->Unref();

            PRINT_INFO("Ipv4 address: " <<  address << std::endl);
        }
    }
}

void printHelpAndQuit(){

    PRINT_INFO("Usage: nve_simulator --filename <file>  [--verbose]\n" << "--help    Print this help message.\n"
              << "--filename <file>     Give filename (mandatory)\n"
              << "--verbose     Print info about configuration" << std::endl);

    exit(EXIT_SUCCESS);

}


