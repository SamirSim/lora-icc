/*
 * This script simulates a complex scenario with multiple gateways and end
 * devices. The metric of interest for this script is the throughput of the
 * network.
 */

#include "ns3/end-device-lora-phy.h"
#include "ns3/gateway-lora-phy.h"
#include "ns3/class-a-end-device-lorawan-mac.h"
#include "ns3/gateway-lorawan-mac.h"
#include "ns3/simulator.h"
#include "ns3/log.h"
#include "ns3/constant-position-mobility-model.h"
#include "ns3/lora-helper.h"
#include "ns3/mobility-helper.h"
#include "ns3/node-container.h"
#include "ns3/position-allocator.h"
#include "ns3/periodic-sender-helper.h"
#include "ns3/command-line.h"
#include "ns3/basic-energy-source-helper.h"
#include "ns3/lora-radio-energy-model-helper.h"
#include "ns3/file-helper.h"
#include "ns3/names.h"
#include <algorithm>
#include <ctime>

using namespace ns3;
using namespace lorawan;

NS_LOG_COMPONENT_DEFINE ("lora-telemetry");

int
main (int argc, char *argv[]) {
  // Network settings
  int nSta = 1;
  int nGateways = 1;
  double distance =100;
  double simulationTime = 36000;
  double payloadSize = 23;
  int period = 3600; //One packet per hour

  bool energyRatio = false;
  bool successRate = false;
  bool batteryLife = false;

  double SF=0; // If SF=0, then the SF is set automatically

  CommandLine cmd;
  cmd.AddValue ("nSta", "Number of end devices to include in the simulation", nSta);
  cmd.AddValue ("distance", "The distance of the area to simulate", distance);
  cmd.AddValue ("SF", "Fixed spreading factor", SF);
  cmd.AddValue ("simulationTime", "The time for which to simulate", simulationTime);
  cmd.AddValue ("payloadSize", "The size of the payload", payloadSize);
  cmd.AddValue ("period", "The period in seconds to be used by periodically transmitting applications", period);
  cmd.AddValue("energyRatio", "Energy ratio in Joules/Byte", energyRatio);
  cmd.AddValue("successRate", "Percentage of successful packets", successRate);
  cmd.AddValue("batteryLife", "Percentage of successful packets", batteryLife);
  cmd.Parse (argc, argv);

  if (energyRatio) {
    LogComponentEnable ("lora-telemetry", LOG_LEVEL_ALL);
    LogComponentEnable ("LoraRadioEnergyModel", LOG_LEVEL_ALL);
    //LogComponentEnable("LoraPhy", LOG_LEVEL_ALL);
  }

  LogComponentEnableAll (LOG_PREFIX_FUNC);
  LogComponentEnableAll (LOG_PREFIX_NODE);
  LogComponentEnableAll (LOG_PREFIX_TIME);

  // Mobility
  MobilityHelper mobility;

  Ptr<ListPositionAllocator> allocator2 = CreateObject<ListPositionAllocator> ();
  for (uint32_t i = 0; i < nSta; i++) {
    allocator2->Add (Vector (distance,0,0));
  }
  mobility.SetPositionAllocator (allocator2);
  mobility.SetMobilityModel ("ns3::ConstantPositionMobilityModel");


  // Create the lora channel object
  Ptr<LogDistancePropagationLossModel> loss = CreateObject<LogDistancePropagationLossModel> ();
  loss->SetPathLossExponent (3.76);
  loss->SetReference (1, 7.7);

  Ptr<PropagationDelayModel> delay = CreateObject<ConstantSpeedPropagationDelayModel> ();

  Ptr<LoraChannel> channel = CreateObject<LoraChannel> (loss, delay);

  // Create the LoraPhyHelper
  LoraPhyHelper phyHelper = LoraPhyHelper ();
  phyHelper.SetChannel (channel);

  // Create the LorawanMacHelper
  LorawanMacHelper macHelper = LorawanMacHelper ();

  // Create the LoraHelper
  LoraHelper helper = LoraHelper ();
  helper.EnablePacketTracking (); // Output filename

  // Create a set of nodes
  NodeContainer endDevices;
  endDevices.Create (nSta);

  // Assign a mobility model to each node
  mobility.Install (endDevices);

  // Create the LoraNetDevices of the end devices
  phyHelper.SetDeviceType (LoraPhyHelper::ED);
  macHelper.SetDeviceType (LorawanMacHelper::ED_A);
  NetDeviceContainer endDevicesNetDevices = helper.Install (phyHelper, macHelper, endDevices);

  // Create the gateway nodes (allocate them uniformely on the disc)
  NodeContainer gateways;
  gateways.Create (nGateways);

  Ptr<ListPositionAllocator> allocator = CreateObject<ListPositionAllocator> ();
  // Make it so that nodes are at a certain height > 0
  allocator->Add (Vector (0.0, 0.0, 0.0));
  mobility.SetPositionAllocator (allocator);
  mobility.Install (gateways);

  // Create a netdevice for each gateway
  phyHelper.SetDeviceType (LoraPhyHelper::GW);
  macHelper.SetDeviceType (LorawanMacHelper::GW);
  helper.Install (phyHelper, macHelper, gateways);

  macHelper.SetSpreadingFactorsUp (endDevices, gateways, channel, SF);

  NS_LOG_DEBUG ("Completed configuration");

  Time appStopTime = Seconds (simulationTime);
  PeriodicSenderHelper appHelper = PeriodicSenderHelper ();
  appHelper.SetPeriod (Seconds (period));
  appHelper.SetPacketSize (payloadSize);
  ApplicationContainer appContainer = appHelper.Install (endDevices);

  if (energyRatio) {    
    BasicEnergySourceHelper basicSourceHelper;
    LoraRadioEnergyModelHelper radioEnergyHelper;

    // configure energy source
    basicSourceHelper.Set ("BasicEnergySourceInitialEnergyJ", DoubleValue (10000)); // Energy in Joules
    basicSourceHelper.Set ("BasicEnergySupplyVoltageV", DoubleValue (3.3));

    radioEnergyHelper.Set ("StandbyCurrentA", DoubleValue (0.0014)); // Idle mode
    radioEnergyHelper.Set ("TxCurrentA", DoubleValue (0.028));
    radioEnergyHelper.Set ("SleepCurrentA", DoubleValue (0.0000015));
    radioEnergyHelper.Set ("RxCurrentA", DoubleValue (0.0112));

    //radioEnergyHelper.SetTxCurrentModel ("ns3::ConstantLoraTxCurrentModel",
      //                                  "TxCurrent", DoubleValue (0.028));

    radioEnergyHelper.SetTxCurrentModel ("ns3::LinearLoraTxCurrentModel");

    // install source on EDs' nodes
    EnergySourceContainer sources = basicSourceHelper.Install (endDevices);
    Names::Add ("/Names/EnergySource", sources.Get (0));

    // install device model
    DeviceEnergyModelContainer deviceModels = radioEnergyHelper.Install (endDevicesNetDevices, sources);
    
    if (batteryLife) {
      FileHelper fileHelper;
      fileHelper.ConfigureFile ("battery-level", FileAggregator::SPACE_SEPARATED);
      fileHelper.WriteProbe ("ns3::DoubleProbe", "/Names/EnergySource/RemainingEnergy", "Output");
    } 
  }
  
  Simulator::Stop (appStopTime);

  NS_LOG_INFO ("Running simulation...");

  Simulator::Run ();

  Simulator::Destroy ();

  if (energyRatio || successRate) {
    LoraPacketTracker &tracker = helper.GetPacketTracker ();
    std::cout << tracker.CountMacPacketsGlobally (Seconds (0), appStopTime) << std::endl;
  }

  return 0;
}