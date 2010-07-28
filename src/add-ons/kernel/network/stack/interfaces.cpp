/*
 * Copyright 2006-2010, Haiku, Inc. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Axel Dörfler, axeld@pinc-software.de
 */


#include "interfaces.h"

#include <net/if_dl.h>
#include <new>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/sockio.h>

#include <KernelExport.h>

#include <lock.h>
#include <net_device.h>
#include <util/AutoLock.h>

#include "device_interfaces.h"
#include "domains.h"
#include "stack_private.h"
#include "utility.h"


//#define TRACE_INTERFACES
#ifdef TRACE_INTERFACES
#	define TRACE(x...) dprintf(STACK_DEBUG_PREFIX x)
#else
#	define TRACE(x...) ;
#endif


struct AddressHashDefinition {
	typedef const sockaddr* KeyType;
	typedef InterfaceAddress ValueType;

	AddressHashDefinition()
	{
	}

	size_t HashKey(const KeyType& key) const
	{
		net_domain* domain = get_domain(key->sa_family);
		if (domain == NULL)
			return 0;

		return domain->address_module->hash_address(key, false);
	}

	size_t Hash(InterfaceAddress* address) const
	{
		return address->domain->address_module->hash_address(address->local, false);
	}

	bool Compare(const KeyType& key, InterfaceAddress* address) const
	{
		if (address->local == NULL)
			return key->sa_family == AF_UNSPEC;

		if (address->local->sa_family != key->sa_family)
			return false;

		return address->domain->address_module->equal_addresses(key,
			address->local);
	}

	InterfaceAddress*& GetLink(InterfaceAddress* address) const
	{
		return address->HashTableLink();
	}
};

typedef BOpenHashTable<AddressHashDefinition, true, true> AddressTable;


static mutex sLock;
static InterfaceList sInterfaces;
static mutex sHashLock;
static AddressTable sAddressTable;
static uint32 sInterfaceIndex;


#if ENABLE_DEBUGGER_COMMANDS


static int
dump_interface(int argc, char** argv)
{
	if (argc != 2) {
		kprintf("usage: %s [address]\n", argv[0]);
		return 0;
	}

	Interface* interface = (Interface*)parse_expression(argv[1]);
	interface->Dump();

	return 0;
}


static int
dump_interfaces(int argc, char** argv)
{
	InterfaceList::Iterator iterator = sInterfaces.GetIterator();
	while (Interface* interface = iterator.Next()) {
		kprintf("%p  %s\n", interface, interface->name);
	}
	return 0;
}


static int
dump_local(int argc, char** argv)
{
	AddressTable::Iterator iterator = sAddressTable.GetIterator();
	size_t i = 0;
	while (InterfaceAddress* address = iterator.Next()) {
		address->Dump(++i);
		dprintf("    hash:          %lu\n",
			address->domain->address_module->hash_address(address->local,
				false));
	}
	return 0;
}


static int
dump_route(int argc, char** argv)
{
	if (argc != 2) {
		kprintf("usage: %s [address]\n", argv[0]);
		return 0;
	}

	net_route* route = (net_route*)parse_expression(argv[1]);
	kprintf("destination:       %p\n", route->destination);
	kprintf("mask:              %p\n", route->mask);
	kprintf("gateway:           %p\n", route->gateway);
	kprintf("flags:             %" B_PRIx32 "\n", route->flags);
	kprintf("mtu:               %" B_PRIu32 "\n", route->mtu);
	kprintf("interface address: %p\n", route->interface_address);

	if (route->interface_address != NULL) {
		((InterfaceAddress*)route->interface_address)->Dump();
	}

	return 0;
}


#endif	// ENABLE_DEBUGGER_COMMANDS


InterfaceAddress::InterfaceAddress()
{
	_Init(NULL, NULL);
}


InterfaceAddress::InterfaceAddress(net_interface* netInterface,
	net_domain* netDomain)
{
	_Init(netInterface, netDomain);
}


InterfaceAddress::~InterfaceAddress()
{
}


status_t
InterfaceAddress::SetTo(const ifaliasreq& request)
{
	status_t status = SetLocal((const sockaddr*)&request.ifra_addr);
	if (status == B_OK)
		status = SetDestination((const sockaddr*)&request.ifra_broadaddr);	
	if (status == B_OK)
		status = SetMask((const sockaddr*)&request.ifra_mask);

	return status;
}


status_t
InterfaceAddress::SetLocal(const sockaddr* to)
{
	return Set(&local, to);
}


status_t
InterfaceAddress::SetDestination(const sockaddr* to)
{
	return Set(&destination, to);
}


status_t
InterfaceAddress::SetMask(const sockaddr* to)
{
	return Set(&mask, to);
}


sockaddr**
InterfaceAddress::AddressFor(int32 option)
{
	switch (option) {
		case SIOCSIFADDR:
		case SIOCGIFADDR:
			return &local;

		case SIOCSIFNETMASK:
		case SIOCGIFNETMASK:
			return &mask;

		case SIOCSIFBRDADDR:
		case SIOCSIFDSTADDR:
		case SIOCGIFBRDADDR:
		case SIOCGIFDSTADDR:
			return &destination;

		default:
			return NULL;
	}
}


#if ENABLE_DEBUGGER_COMMANDS


void
InterfaceAddress::Dump(size_t index, bool hideInterface)
{
	if (index)
		kprintf("%2zu. ", index);
	else
		kprintf("    ");
	
	if (!hideInterface) {
		kprintf("interface:   %p (%s)\n    ", interface,
			interface != NULL ? interface->name : "-");
	}

	kprintf("domain:      %p (family %u)\n", domain,
		domain != NULL ? domain->family : AF_UNSPEC);

	char buffer[64];
	if (local != NULL && domain != NULL) {
		domain->address_module->print_address_buffer(local, buffer,
			sizeof(buffer), false);
	} else
		strcpy(buffer, "-");
	kprintf("    local:       %s\n", buffer);

	if (mask != NULL && domain != NULL) {
		domain->address_module->print_address_buffer(mask, buffer,
			sizeof(buffer), false);
	} else
		strcpy(buffer, "-");
	kprintf("    mask:        %s\n", buffer);

	if (destination != NULL && domain != NULL) {
		domain->address_module->print_address_buffer(destination, buffer,
			sizeof(buffer), false);
	} else
		strcpy(buffer, "-");
	kprintf("    destination: %s\n", buffer);
}


#endif	// ENABLE_DEBUGGER_COMMANDS


/*static*/ status_t
InterfaceAddress::Set(sockaddr** _address, const sockaddr* to)
{
	sockaddr* address = *_address;

	if (to == NULL || to->sa_family == AF_UNSPEC) {
		// Clear address
		free(address);
		*_address = NULL;
		return B_OK;
	}

	// Set address

	size_t size = max_c(to->sa_len, sizeof(sockaddr));
	if (size > sizeof(sockaddr_storage))
		size = sizeof(sockaddr_storage);

	address = Prepare(_address, size);
	if (address == NULL)
		return B_NO_MEMORY;

	memcpy(address, to, size);
	address->sa_len = size;

	return B_OK;
}


/*static*/ sockaddr*
InterfaceAddress::Prepare(sockaddr** _address, size_t size)
{
	size = max_c(size, sizeof(sockaddr));
	if (size > sizeof(sockaddr_storage))
		size = sizeof(sockaddr_storage);

	sockaddr* address = *_address;

	if (address == NULL || size > address->sa_len) {
		address = (sockaddr*)realloc(address, size);
		if (address == NULL)
			return NULL;
	}

	address->sa_len = size;

	*_address = address;
	return address;
}


void
InterfaceAddress::_Init(net_interface* netInterface, net_domain* netDomain)
{
	interface = netInterface;
	domain = netDomain;
	local = NULL;
	destination = NULL;
	mask = NULL;
}


// #pragma mark -


Interface::Interface(const char* interfaceName,
	net_device_interface* deviceInterface)
{
	TRACE("Interface %p: new \"%s\", device interface %p\n", this,
		interfaceName, deviceInterface);

	strlcpy(name, interfaceName, IF_NAMESIZE);
	device = deviceInterface->device;

	index = ++sInterfaceIndex;
	flags = 0;
	type = 0;
	mtu = deviceInterface->device->mtu;
	metric = 0;

	fDeviceInterface = acquire_device_interface(deviceInterface);

	recursive_lock_init(&fLock, name);

	// Grab a reference to the networking stack, to make sure it won't be
	// unloaded as long as an interface exists
	module_info* module;
	get_module(gNetStackInterfaceModule.info.name, &module);
}


Interface::~Interface()
{
	recursive_lock_destroy(&fLock);

	// Release reference of the stack - at this point, our stack may be unloaded
	// if no other interfaces or sockets are left
	put_module(gNetStackInterfaceModule.info.name);
}


/*!	Returns a reference to the first InterfaceAddress that is from the same
	as the specified \a family.
*/
InterfaceAddress*
Interface::FirstForFamily(int family)
{
	RecursiveLocker locker(fLock);

	InterfaceAddress* address = _FirstForFamily(family);
	if (address != NULL) {
		address->AcquireReference();
		return address;
	}

	return NULL;
}


/*!	Returns a reference to the first unconfigured address of this interface
	for the specified \a family.
*/
InterfaceAddress*
Interface::FirstUnconfiguredForFamily(int family)
{
	RecursiveLocker locker(fLock);

	AddressList::Iterator iterator = fAddresses.GetIterator();
	while (InterfaceAddress* address = iterator.Next()) {
		if (address->domain->family == family
			&& (address->local == NULL
				// TODO: this has to be solved differently!!
				|| (flags & IFF_CONFIGURING) != 0)) {
			address->AcquireReference();
			return address;
		}
	}

	return NULL;
}


/*!	Returns a reference to the InterfaceAddress that has the specified
	\a destination address.
*/
InterfaceAddress*
Interface::AddressForDestination(net_domain* domain,
	const sockaddr* destination)
{
	RecursiveLocker locker(fLock);

	if ((device->flags & IFF_BROADCAST) == 0) {
		// The device does not support broadcasting
		return NULL;
	}

	AddressList::Iterator iterator = fAddresses.GetIterator();
	while (InterfaceAddress* address = iterator.Next()) {
		if (address->domain == domain
			&& address->destination != NULL
			&& domain->address_module->equal_addresses(address->destination,
					destination)) {
			address->AcquireReference();
			return address;
		}
	}

	return NULL;
}


status_t
Interface::AddAddress(InterfaceAddress* address)
{
	net_domain* domain = address->domain;
	if (domain == NULL)
		return B_BAD_VALUE;

	RecursiveLocker locker(fLock);
	fAddresses.Add(address);
	locker.Unlock();
	
	MutexLocker hashLocker(sHashLock);
	sAddressTable.Insert(address);
	return B_OK;
}


void
Interface::RemoveAddress(InterfaceAddress* address)
{
	net_domain* domain = address->domain;
	if (domain == NULL)
		return;

	RecursiveLocker locker(fLock);

	fAddresses.Remove(address);
	address->GetDoublyLinkedListLink()->next = NULL;

// TODO!
//	if (_FirstForFamily(domain->family) == NULL)
//		put_domain_datalink_protocols(this, domain);

	locker.Unlock();
	
	MutexLocker hashLocker(sHashLock);
	sAddressTable.Remove(address);
}


bool
Interface::GetNextAddress(InterfaceAddress** _address)
{
	RecursiveLocker locker(fLock);

	InterfaceAddress* address = *_address;
	if (address == NULL) {
		// get first address
		address = fAddresses.First();
	} else {
		// get next, if possible
		InterfaceAddress* next = fAddresses.GetNext(address);
		address->ReleaseReference();
		address = next;
	}

	*_address = address;

	if (address == NULL)
		return false;

	address->AcquireReference();
	return true;
}


status_t
Interface::Control(net_domain* domain, int32 option, ifreq& request,
	ifreq* userRequest, size_t length)
{
	switch (option) {
		case SIOCSIFFLAGS:
		{
			uint32 requestFlags = request.ifr_flags;
			uint32 oldFlags = flags;
			status_t status = B_OK;

			request.ifr_flags &= ~(IFF_UP | IFF_LINK | IFF_BROADCAST);

			if ((requestFlags & IFF_UP) != (flags & IFF_UP)) {
				if ((requestFlags & IFF_UP) != 0)
					status = _SetUp();
				else
					_SetDown();
			}

			if (status == B_OK) {
				// TODO: maybe allow deleting IFF_BROADCAST on the interface
				// level?
				flags &= IFF_UP | IFF_LINK | IFF_BROADCAST;
				flags |= request.ifr_flags;
			}

			if (oldFlags != flags)
				notify_interface_changed(this, oldFlags, flags);

			return status;
		}
		
		case SIOCSIFADDR:
		case SIOCSIFNETMASK:
		case SIOCSIFBRDADDR:
		case SIOCSIFDSTADDR:
		case SIOCDIFADDR:
		{
			RecursiveLocker locker(fLock);
			
			InterfaceAddress* address = NULL;
			sockaddr_storage oldAddress;
			sockaddr_storage newAddress;

			size_t size = max_c(request.ifr_addr.sa_len, sizeof(sockaddr));
			if (size > sizeof(sockaddr_storage))
				size = sizeof(sockaddr_storage);

			if (user_memcpy(&newAddress, &userRequest->ifr_addr, size) != B_OK)
				return B_BAD_ADDRESS;

			if (option == SIOCDIFADDR) {
				// Find referring address - we can't use the hash, as another
				// interface might use the same address.
				AddressList::Iterator iterator = fAddresses.GetIterator();
				while ((address = iterator.Next()) != NULL) {
					if (address->domain == domain
						&& domain->address_module->equal_addresses(
							address->local, (sockaddr*)&newAddress))
						break;
				}
			} else {
				// Just use the first address for this family
				address = _FirstForFamily(domain->family);
			}

			if (address == NULL)
				return B_BAD_VALUE;

			if (*address->AddressFor(option) != NULL)
				memcpy(&oldAddress, *address->AddressFor(option), size);
			else
				oldAddress.ss_family = AF_UNSPEC;

			// TODO: mark this address busy or call while holding the lock!
			address->AcquireReference();
			locker.Unlock();

			domain_datalink* datalink = DomainDatalink(domain->family);
			status_t status = datalink->first_protocol->module->change_address(
				datalink->first_protocol, address, option,
				oldAddress.ss_family != AF_UNSPEC
					? (sockaddr*)&oldAddress : NULL,
				option != SIOCDIFADDR ? (sockaddr*)&newAddress : NULL);

			address->ReleaseReference();
			return status;
		}

		default:
			// pass the request into the datalink protocol stack
			domain_datalink* datalink = DomainDatalink(domain->family);
			if (datalink->first_info != NULL) {
				return datalink->first_info->control(
					datalink->first_protocol, option, userRequest, length);
			}
			break;
	}

	return B_BAD_VALUE;
}


status_t
Interface::CreateDomainDatalinkIfNeeded(net_domain* domain)
{
	RecursiveLocker locker(fLock);
	
	if (fDatalinkTable.Lookup(domain->family) != NULL)
		return B_OK;

	TRACE("Interface %p: create domain datalink for domain %p\n", this, domain);

	domain_datalink* datalink = new(std::nothrow) domain_datalink;
	if (datalink == NULL)
		return B_NO_MEMORY;

	datalink->domain = domain;

	// setup direct route for bound devices
	datalink->direct_route.destination = NULL;
	datalink->direct_route.mask = NULL;
	datalink->direct_route.gateway = NULL;
	datalink->direct_route.flags = 0;
	datalink->direct_route.mtu = 0;
	datalink->direct_route.interface_address = &datalink->direct_address;
	datalink->direct_route.ref_count = 1;
		// make sure this doesn't get deleted accidently

	// provide its link back to the interface
	datalink->direct_address.local = NULL;
	datalink->direct_address.destination = NULL;
	datalink->direct_address.mask = NULL;
	datalink->direct_address.domain = domain;
	datalink->direct_address.interface = this;

	fDatalinkTable.Insert(datalink);

	status_t status = get_domain_datalink_protocols(this, domain);
	if (status == B_OK)
		return B_OK;

	fDatalinkTable.Remove(datalink);
	delete datalink;

	return status;
}


domain_datalink*
Interface::DomainDatalink(uint8 family)
{
	// Note: domain datalinks cannot be removed while the interface is alive,
	// since this would require us either to hold the lock while calling this
	// function, or introduce reference counting for the domain_datalink
	// structure. 
	RecursiveLocker locker(fLock);
	return fDatalinkTable.Lookup(family);
}


#if ENABLE_DEBUGGER_COMMANDS


void
Interface::Dump() const
{
	kprintf("name:                %s\n", name);
	kprintf("device:              %p\n", device);
	kprintf("device_interface:    %p\n", fDeviceInterface);
	kprintf("index:               %" B_PRIu32 "\n", index);
	kprintf("flags:               %#" B_PRIx32 "\n", flags);
	kprintf("type:                %u\n", type);
	kprintf("mtu:                 %" B_PRIu32 "\n", mtu);
	kprintf("metric:              %" B_PRIu32 "\n", metric);

	kprintf("datalink protocols:\n");

	DatalinkTable::Iterator datalinkIterator = fDatalinkTable.GetIterator();
	size_t i = 0;
	while (domain_datalink* datalink = datalinkIterator.Next()) {
		kprintf("%2zu. domain:          %p\n", ++i, datalink->domain);
		kprintf("    first_protocol:  %p\n", datalink->first_protocol);
		kprintf("    first_info:      %p\n", datalink->first_info);
		kprintf("    direct_route:    %p\n", &datalink->direct_route);
	}

	kprintf("addresses:\n");

	AddressList::ConstIterator iterator = fAddresses.GetIterator();
	i = 0;
	while (InterfaceAddress* address = iterator.Next()) {
		address->Dump(++i, true);
	}
}


#endif	// ENABLE_DEBUGGER_COMMANDS


status_t
Interface::_SetUp()
{
	status_t status = up_device_interface(fDeviceInterface);
	if (status != B_OK)
		return status;

	// propagate flag to all datalink protocols

	RecursiveLocker locker(fLock);

	DatalinkTable::Iterator iterator = fDatalinkTable.GetIterator();
	while (domain_datalink* datalink = iterator.Next()) {
		status = datalink->first_info->interface_up(datalink->first_protocol);
		if (status != B_OK) {
			// Revert "up" status
			DatalinkTable::Iterator secondIterator
				= fDatalinkTable.GetIterator();
			while (secondIterator.HasNext()) {
				domain_datalink* secondDatalink = secondIterator.Next();
				if (secondDatalink == NULL || secondDatalink == datalink)
					break;

				secondDatalink->first_info->interface_down(
					secondDatalink->first_protocol);
			}

			down_device_interface(fDeviceInterface);
			return status;
		}
	}

	flags |= IFF_UP;
	return B_OK;
}


void
Interface::_SetDown()
{
	if ((flags & IFF_UP) == 0)
		return;

	RecursiveLocker locker(fLock);

	DatalinkTable::Iterator iterator = fDatalinkTable.GetIterator();
	while (domain_datalink* datalink = iterator.Next()) {
		datalink->first_info->interface_down(datalink->first_protocol);
	}

	down_device_interface(fDeviceInterface);
	flags &= ~IFF_UP;
}


InterfaceAddress*
Interface::_FirstForFamily(int family)
{
	ASSERT_LOCKED_RECURSIVE(&fLock);

	AddressList::Iterator iterator = fAddresses.GetIterator();
	while (InterfaceAddress* address = iterator.Next()) {
		if (address->domain != NULL && address->domain->family == family)
			return address;
	}

	return NULL;
}


// #pragma mark -


/*!	Searches for a specific interface by name.
	You need to have the interface list's lock hold when calling this function.
*/
static struct Interface*
find_interface(const char* name)
{
	ASSERT_LOCKED_MUTEX(&sLock);

	InterfaceList::Iterator iterator = sInterfaces.GetIterator();
	while (Interface* interface = iterator.Next()) {
		if (!strcmp(interface->name, name))
			return interface;
	}

	return NULL;
}


/*!	Searches for a specific interface by index.
	You need to have the interface list's lock hold when calling this function.
*/
static struct Interface*
find_interface(uint32 index)
{
	InterfaceList::Iterator iterator = sInterfaces.GetIterator();
	while (Interface* interface = iterator.Next()) {
		if (interface->index == index)
			return interface;
	}

	return NULL;
}


/*!	Removes the default routes as set by add_default_routes() again. */
static void
remove_default_routes(InterfaceAddress* address, int32 option)
{
	net_route route;
	route.destination = address->local;
	route.gateway = NULL;
	route.interface_address = address;

	if (address->mask != NULL
		&& (option == SIOCSIFNETMASK || option == SIOCSIFADDR)) {
		route.mask = address->mask;
		route.flags = 0;
		remove_route(address->domain, &route);
	}

	if (option == SIOCSIFADDR) {
		route.mask = NULL;
		route.flags = RTF_LOCAL | RTF_HOST;
		remove_route(address->domain, &route);
	}
}


/*!	Adds the default routes that every interface address needs, ie. the local
	host route, and one for the subnet (if set).
*/
static void
add_default_routes(InterfaceAddress* address, int32 option)
{
	net_route route;
	route.destination = address->local;
	route.gateway = NULL;
	route.interface_address = address;

	if (address->mask != NULL
		&& (option == SIOCSIFNETMASK || option == SIOCSIFADDR)) {
		route.mask = address->mask;
		route.flags = 0;
		add_route(address->domain, &route);
	}

	if (option == SIOCSIFADDR) {
		route.mask = NULL;
		route.flags = RTF_LOCAL | RTF_HOST;
		add_route(address->domain, &route);
	}
}


// #pragma mark -


status_t
add_interface(const char* name, net_domain_private* domain,
	const ifaliasreq& request, net_device_interface* deviceInterface)
{
	MutexLocker locker(sLock);
	
	if (find_interface(name) != NULL)
		return B_NAME_IN_USE;

	Interface* interface
		= new(std::nothrow) Interface(name, deviceInterface);
	if (interface == NULL)
		return B_NO_MEMORY;

	sInterfaces.Add(interface);
	interface->AcquireReference();
		// We need another reference to be able to use the interface without
		// holding sLock.

	locker.Unlock();

	notify_interface_added(interface);
	add_interface_address(interface, domain, request);

	interface->ReleaseReference();

	return B_OK;
}


/*!	Removes the interface from the list, and puts the stack's reference to it.
*/
status_t
remove_interface(Interface* interface)
{
	return B_ERROR;
#if 0
	// deleting an interface is fairly complex as we need
	// to clear all references to it throughout the stack

	// this will possibly call (if IFF_UP):
	//  interface_protocol_down()
	//   domain_interface_went_down()
	//    invalidate_routes()
	//     remove_route()
	//      update_route_infos()
	//       get_route_internal()
	//   down_device_interface() -- if upcount reaches 0
	interface_set_down(interface);

	// This call requires the RX Lock to be a recursive
	// lock since each uninit_protocol() call may call
	// again into the stack to unregister a reader for
	// instance, which tries to obtain the RX lock again.
	put_domain_datalink_protocols(interface);

	put_device_interface(interface->device_interface);

	delete interface;
#endif
#if 0
	invalidate_routes(domain, interface);

	list_remove_item(&domain->interfaces, interface);
	notify_interface_removed(interface);
	delete_interface((net_interface_private*)interface);
#endif
}


void
interface_went_down(Interface* interface)
{
	TRACE("interface_went_down(%s)\n", interface->name);
	// TODO!
	//invalidate_routes(domain, interface);
}


status_t
add_interface_address(Interface* interface, net_domain_private* domain,
	const ifaliasreq& request)
{
	// Make sure the family of the provided addresses is valid
	if ((request.ifra_addr.ss_family != domain->family
			&& request.ifra_addr.ss_family != AF_UNSPEC)
		|| (request.ifra_mask.ss_family != domain->family
			&& request.ifra_mask.ss_family != AF_UNSPEC)
		|| (request.ifra_broadaddr.ss_family != domain->family
			&& request.ifra_broadaddr.ss_family != AF_UNSPEC))
		return B_BAD_VALUE;

	RecursiveLocker locker(interface->Lock());

	InterfaceAddress* address
		= new(std::nothrow) InterfaceAddress(interface, domain);
	if (address == NULL)
		return B_NO_MEMORY;

	status_t status = address->SetTo(request);
	if (status == B_OK)
		status = interface->CreateDomainDatalinkIfNeeded(domain);
	if (status == B_OK)
		status = interface->AddAddress(address);

	if (status == B_OK && address->local != NULL) {
		// update the datalink protocols
		domain_datalink* datalink = interface->DomainDatalink(domain->family);

		status = datalink->first_protocol->module->change_address(
			datalink->first_protocol, address, SIOCAIFADDR, NULL,
			address->local);
		if (status != B_OK)
			interface->RemoveAddress(address);
	}
	if (status == B_OK)
		notify_interface_changed(interface);
	else
		delete address;

	return status;
}


status_t
update_interface_address(InterfaceAddress* interfaceAddress, int32 option,
	const sockaddr* oldAddress, const sockaddr* newAddress)
{
	MutexLocker locker(sHashLock);

	// set logical interface address
	sockaddr** _address = interfaceAddress->AddressFor(option);
	if (_address == NULL)
		return B_BAD_VALUE;

	remove_default_routes(interfaceAddress, option);
	sAddressTable.Remove(interfaceAddress);

	// Copy new address over
	status_t status = InterfaceAddress::Set(_address, newAddress);
	if (status == B_OK) {
		sockaddr* address = *_address;

		if (option == SIOCSIFADDR || option == SIOCSIFNETMASK) {
			// Reset netmask and broadcast addresses to defaults
			net_domain* domain = interfaceAddress->domain;
			sockaddr* netmask = NULL;
			const sockaddr* oldNetmask = NULL;
			if (option == SIOCSIFADDR) {
				netmask = InterfaceAddress::Prepare(
					&interfaceAddress->mask, address->sa_len);
			} else {
				oldNetmask = oldAddress;
				netmask = interfaceAddress->mask;
			}

			// Reset the broadcast address if the address family has
			// such
			sockaddr* broadcast = NULL;
			if ((domain->address_module->flags
					& NET_ADDRESS_MODULE_FLAG_BROADCAST_ADDRESS) != 0) {
				broadcast = InterfaceAddress::Prepare(
					&interfaceAddress->destination, address->sa_len);
			} else
				InterfaceAddress::Set(&interfaceAddress->destination, NULL);

			domain->address_module->set_to_defaults(netmask, broadcast,
				interfaceAddress->local, oldNetmask);
		}

		add_default_routes(interfaceAddress, option);
		notify_interface_changed(interfaceAddress->interface);
	}

	sAddressTable.Insert(interfaceAddress);
	return status;
}


Interface*
get_interface(net_domain* domain, uint32 index)
{
	MutexLocker locker(sLock);

	if (index == 0)
		return sInterfaces.First();
	
	Interface* interface = find_interface(index);
	if (interface == NULL)
		return NULL;

	if (interface->CreateDomainDatalinkIfNeeded(domain) != B_OK)
		return NULL;

	interface->AcquireReference();
	return interface;
}


Interface*
get_interface(net_domain* domain, const char* name)
{
	MutexLocker locker(sLock);

	Interface* interface = find_interface(name);
	if (interface == NULL)
		return NULL;

	if (interface->CreateDomainDatalinkIfNeeded(domain) != B_OK)
		return NULL;

	interface->AcquireReference();
	return interface;
}


Interface*
get_interface_for_device(net_domain* domain, uint32 index)
{
	MutexLocker locker(sLock);

	InterfaceList::Iterator iterator = sInterfaces.GetIterator();
	while (Interface* interface = iterator.Next()) {
		if (interface->device->index == index) {
			if (interface->CreateDomainDatalinkIfNeeded(domain) != B_OK)
				return NULL;

			interface->AcquireReference();
			return interface;
		}
	}
	
	return NULL;
}


InterfaceAddress*
get_interface_address(const sockaddr* local)
{
	if (local->sa_family == AF_UNSPEC)
		return NULL;

	MutexLocker locker(sHashLock);

	InterfaceAddress* address = sAddressTable.Lookup(local);
	if (address == NULL)
		return NULL;

	address->AcquireReference();
	return address;
}


InterfaceAddress*
get_interface_address_for_destination(net_domain* domain,
	const struct sockaddr* destination)
{
	MutexLocker locker(sLock);
	
	InterfaceList::Iterator iterator = sInterfaces.GetIterator();
	while (Interface* interface = iterator.Next()) {
		InterfaceAddress* address
			= interface->AddressForDestination(domain, destination);
		if (address != NULL)
			return address;
	}

	return NULL;
}


InterfaceAddress*
get_interface_address_for_link(net_domain* domain,
	const struct sockaddr* _linkAddress, bool unconfiguredOnly)
{
	sockaddr_dl& linkAddress = *(sockaddr_dl*)_linkAddress;

	MutexLocker locker(sLock);

	InterfaceList::Iterator iterator = sInterfaces.GetIterator();
	while (Interface* interface = iterator.Next()) {
		if (linkAddress.sdl_alen == interface->device->address.length
			&& memcmp(LLADDR(&linkAddress), interface->device->address.data,
				linkAddress.sdl_alen) == 0) {
			// link address matches
			if (unconfiguredOnly)
				return interface->FirstUnconfiguredForFamily(domain->family);

			return interface->FirstForFamily(domain->family);
		}
	}

	return NULL;
}


uint32
count_interfaces()
{
	MutexLocker locker(sLock);

	return sInterfaces.Count();
}


/*!	Dumps a list of all interfaces into the supplied userland buffer.
	If the interfaces don't fit into the buffer, an error (\c ENOBUFS) is
	returned.
*/
status_t
list_interfaces(int family, void* _buffer, size_t* bufferSize)
{
	MutexLocker locker(sLock);

	UserBuffer buffer(_buffer, *bufferSize);

	InterfaceList::Iterator iterator = sInterfaces.GetIterator();
	while (Interface* interface = iterator.Next()) {
		ifreq request;
		strlcpy(request.ifr_name, interface->name, IF_NAMESIZE);

		InterfaceAddress* address = interface->FirstForFamily(family);
		if (address != NULL && address->local != NULL) {
			// copy actual address
			memcpy(&request.ifr_addr, address->local, address->local->sa_len);
		} else {
			// empty address
			request.ifr_addr.sa_len = 2;
			request.ifr_addr.sa_family = AF_UNSPEC;
		}
		if (address != NULL)
			address->ReleaseReference();

		if (buffer.Copy(&request, IF_NAMESIZE
				+ request.ifr_addr.sa_len) == NULL)
			return buffer.Status();
	}

	*bufferSize = buffer.ConsumedAmount();
	return B_OK;
}


//	#pragma mark -


status_t
init_interfaces()
{
	mutex_init(&sLock, "net interfaces");
	mutex_init(&sHashLock, "net local addresses");

	new (&sInterfaces) InterfaceList;
	new (&sAddressTable) AddressTable;
		// static C++ objects are not initialized in the module startup

#if ENABLE_DEBUGGER_COMMANDS
	add_debugger_command("net_interface", &dump_interface,
		"Dump the given network interface");
	add_debugger_command("net_interfaces", &dump_interfaces,
		"Dump all network interfaces");
	add_debugger_command("net_local", &dump_local,
		"Dump all local interface addresses");
	add_debugger_command("net_route", &dump_route,
		"Dump the given network route");
#endif
	return B_OK;
}


status_t
uninit_interfaces()
{
#if ENABLE_DEBUGGER_COMMANDS
	remove_debugger_command("net_interface", &dump_interface);
#endif

	mutex_destroy(&sLock);
	mutex_destroy(&sHashLock);
	return B_OK;
}

