// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <vector>

#include "base/at_exit.h"
#include "base/bind.h"
#include "base/memory/scoped_ptr.h"
#include "base/message_loop/message_loop.h"
#include "chrome/browser/local_discovery/service_discovery_client_impl.h"
#include "chrome/tools/service_discovery_sniffer/service_discovery_sniffer.h"
#include "net/dns/mdns_client.h"

namespace local_discovery {

ServicePrinter::ServicePrinter(ServiceDiscoveryClient* client,
                               const std::string& service_name)
    : changed_(false) {
  service_resolver_ =
      client->CreateServiceResolver(
          service_name,
          base::Bind(&ServicePrinter::OnServiceResolved,
                     base::Unretained(this)));
}

ServicePrinter::~ServicePrinter() {
}

void ServicePrinter::Added() {
  changed_ = false;
  service_resolver_->StartResolving();
}

void ServicePrinter::Changed() {
  changed_ = true;
  service_resolver_->StartResolving();
}

void ServicePrinter::Removed() {
  printf("Service Removed: %s\n", service_resolver_->GetName().c_str());
}

void ServicePrinter::OnServiceResolved(ServiceResolver::RequestStatus status,
                                       const ServiceDescription& service) {
  if (changed_) {
    printf("Service Updated: %s\n", service.instance_name().c_str());
  } else {
    printf("Service Added: %s\n", service.instance_name().c_str());
  }

  printf("\tAddress: %s:%d\n", service.address.host().c_str(),
         service.address.port());
  printf("\tMetadata: \n");
  for (std::vector<std::string>::const_iterator i = service.metadata.begin();
       i != service.metadata.end(); i++) {
    printf("\t\t%s\n", i->c_str());
  }

  if (service.ip_address != net::IPAddressNumber()) {
    printf("\tIP Address: %s\n", net::IPAddressToString(
        service.ip_address).c_str());
  }
}

ServiceTypePrinter::ServiceTypePrinter(ServiceDiscoveryClient* client,
                                       const std::string& service_type)
    : client_(client)  {
  watcher_ = client_->CreateServiceWatcher(
      service_type, base::Bind(&ServiceTypePrinter::OnServiceUpdated,
                               base::Unretained(this)));
}

void ServiceTypePrinter::Start() {
  watcher_->Start();
  watcher_->DiscoverNewServices(false);
}

ServiceTypePrinter::~ServiceTypePrinter() {
}

void ServiceTypePrinter::OnServiceUpdated(ServiceWatcher::UpdateType update,
                                          const std::string& service_name) {
  if (update == ServiceWatcher::UPDATE_ADDED) {
    services_[service_name].reset(new ServicePrinter(client_, service_name));
    services_[service_name]->Added();
  } else if (update == ServiceWatcher::UPDATE_CHANGED) {
    services_[service_name]->Changed();
  } else if (update == ServiceWatcher::UPDATE_REMOVED) {
    services_[service_name]->Removed();
    services_.erase(service_name);
  }
}

}  // namespace local_discovery

int main(int argc, char** argv) {
  base::AtExitManager at_exit_manager;
  base::MessageLoopForIO message_loop;

  if (argc != 2) {
    printf("Please provide exactly 1 argument.\n");
    return 1;
  }

  scoped_ptr<net::MDnsClient> mdns_client = net::MDnsClient::CreateDefault();
  scoped_ptr<net::MDnsSocketFactory> socket_factory =
      net::MDnsSocketFactory::CreateDefault();
  mdns_client->StartListening(socket_factory.get());
  scoped_ptr<local_discovery::ServiceDiscoveryClient> service_discovery_client;
  service_discovery_client.reset(
      new local_discovery::ServiceDiscoveryClientImpl(mdns_client.get()));
  {
    // To guarantee/make explicit the ordering constraint.
    local_discovery::ServiceTypePrinter print_changes(
        service_discovery_client.get(),
        std::string(argv[1]) + "._tcp.local");

    print_changes.Start();
    message_loop.Run();
  }
}
