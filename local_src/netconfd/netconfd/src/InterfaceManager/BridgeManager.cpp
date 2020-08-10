// SPDX-License-Identifier: GPL-2.0-or-later

#include "BridgeManager.hpp"

#include "CommandExecutor.hpp"
#include "Logger.hpp"

#include <boost/filesystem.hpp>
#include <boost/format.hpp>

#include "EthernetInterface.hpp"

namespace netconf {

using namespace std::literals;

BridgeManager::BridgeManager(IBridgeController &bridge_controller, IDeviceProperties &properies_provider,
                             INetDevManager &netdev_manager)
    : bridge_controller_ { bridge_controller },
      properies_provider_(properies_provider),
      netdev_manager_ { netdev_manager },
      interface_validator_(bridge_controller_),
      bridge_configurator_(bridge_controller_),
      bridge_config_transformator_(properies_provider_),
      switch_config_ { "/etc/switch_settings.conf" },
      switch_settings_file_monitor_ { "/etc/switch_settings.conf", [this](FileMonitor&) {
        this->UpdateAgetime();
      } } {
  SetDefaultInterfaceUp();
  UpdateAgetime();
}

Status BridgeManager::SetDefaultInterfaceUp() const {
  // CAUTION: eth0 has to be up, before ethX? devices can be set up.
  return bridge_controller_.SetInterfaceUp("eth0");
}

Status BridgeManager::ApplyBridgeConfig(BridgeConfig const &os_config) const {

  Status status = SetDefaultInterfaceUp();

  if (status.Ok()) {
    status = bridge_configurator_.Configure(os_config);
  }

  return status;
}

Bridges BridgeManager::GetBridges() const {

  BridgeConfig bridge_config = bridge_configurator_.GetConfiguration();

  Bridges bridges;
  for (auto &bridge_pair : bridge_config) {
    bridges.push_back(bridge_pair.first);
  }

  return bridges;
}

BridgeConfig BridgeManager::GetBridgeConfig() const {

  BridgeConfig config_os = bridge_configurator_.GetConfiguration();
  BridgeConfig config_product = bridge_config_transformator_.ConvertOSToProduct(config_os);
  return config_product;

}

Status BridgeManager::IsValid(BridgeConfig const &product_config) const {

  Status status;

  if (product_config.empty()) {
    status.Prepend(StatusCode::INVALID_CONFIG, "Bridge configuration validation error, empty configuration");
  }

  BridgeConfig os_config;
  if (status.Ok()) {
    os_config = bridge_config_transformator_.ConvertProductToOS(product_config);
    if (os_config.empty()) {
      status.Prepend(StatusCode::INVALID_CONFIG,
                     "Bridge configuration validation error, conversion product to OS bridge config error");
    }
  }

  if (status.Ok()) {
    status = interface_validator_.Validate(os_config);
  }

  return status;

}

Status BridgeManager::ApplyBridgeConfiguration(BridgeConfig &product_config) const {

  Status status = IsValid(product_config);
  if (status.NotOk()) {
    return status;
  }

  BridgeConfig os_config = bridge_config_transformator_.ConvertProductToOS(product_config);
  if (os_config.empty()) {
    status.Prepend(StatusCode::INVALID_CONFIG, "Conversion product to OS bridge config error");
  }

  if (status.Ok()) {
    status = ApplyBridgeConfig(os_config);
  }

  if (status.Ok()) {
    netdev_manager_.ConfigureBridges(product_config);
  }

  if (status.Ok()) {
    UpdateAgetime();
  }

  return status;
}

Status BridgeManager::SetBridgeUp(const Bridge &bridge) const {
  return bridge_configurator_.SetBridgeUp(bridge);
}

Status BridgeManager::SetBridgeDown(const Bridge &bridge) const {
  return bridge_configurator_.SetBridgeDown(bridge);
}

Interfaces BridgeManager::GetBridgeAssignedInterfaces() const {
  return bridge_configurator_.GetBridgeAssignedInterfaces();
}

void BridgeManager::UpdateAgetime() const {
  auto bridges = bridge_controller_.GetBridges();
  auto age_time = switch_config_.GetFastAgeing() ? 0 : 300;
  LogInfo("UpdateAgetime");
  ::std::for_each(bridges.begin(), bridges.end(), [age_time, this](Bridge &b) {
    this->bridge_controller_.SetAgetime(b, age_time);
  });
}

} /* namespace netconf */
