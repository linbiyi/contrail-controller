/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */
#ifndef vnsw_agent_interface_hpp
#define vnsw_agent_interface_hpp

/////////////////////////////////////////////////////////////////////////////
// Agent supports multiple type of interface. Class Interface is defines
// common attributes of all interfaces. All interfaces derive from the base 
// Interface class
/////////////////////////////////////////////////////////////////////////////

struct InterfaceData;

class Interface : AgentRefCount<Interface>, public AgentDBEntry {
public:
    // Type of interfaces supported
    enum Type {
        INVALID,
        // Represents the physical ethernet port. Can be LAG interface also
        PHYSICAL,
        // Interface in the virtual machine
        VM_INTERFACE,
        // The virtual host interfaces created in host-os
        // Example interfaces:
        // vhost0 in case of KVM
        // xap0 in case of XEN
        // vgw in case of Simple Gateway
        VIRTUAL_HOST,
        // pkt0 interface used to exchange packets between vrouter and agent
        PACKET
    };

    enum Trace {
        ADD,
        DELETE,
        ACTIVATED,
        DEACTIVATED,
        FLOATING_IP_CHANGE,
        SERVICE_CHANGE,
    };

    enum MirrorDirection {
        MIRROR_RX_TX,
        MIRROR_RX,
        MIRROR_TX,
        UNKNOWN,
    };

    static const uint32_t kInvalidIndex = 0xFFFFFFFF;

    Interface(Type type, const boost::uuids::uuid &uuid,
              const std::string &name, VrfEntry *vrf);
    virtual ~Interface();

    // DBEntry virtual function. Must be implemented by derived interfaces
    virtual KeyPtr GetDBRequestKey() const = 0;
    //DBEntry virtual function. Implmeneted in base class since its common 
    //for all interfaces
    virtual void SetKey(const DBRequestKey *key);

    // virtual functions for specific interface types
    virtual bool CmpInterface(const DBEntry &rhs) const = 0;
    virtual void SendTrace(Trace event) const;

    // DBEntry comparator virtual function
    bool IsLess(const DBEntry &rhs) const {
        const Interface &intf = static_cast<const Interface &>(rhs);
        if (type_ != intf.type_) {
            return type_ < intf.type_;
        }

        return CmpInterface(rhs);
    }

    uint32_t GetVrfId() const;
    uint32_t GetRefCount() const {
        return AgentRefCount<Interface>::GetRefCount();
    }

    bool DBEntrySandesh(Sandesh *sresp, std::string &name) const;
    void GetOsParams();

    // Tunnelled packets are expected on PHYSICAL interfaces only
    bool IsTunnelEnabled() const { return (type_ == PHYSICAL);}

    // Accessor methods
    Type type() const {return type_;}
    const boost::uuids::uuid &GetUuid() const {return uuid_;}
    const std::string &name() const {return name_;}
    VrfEntry *vrf() const {return vrf_.get();}
    bool active() const {return active_;}
    const uint32_t id() const {return id_;}
    bool dhcp_enabled() const {return dhcp_enabled_;}
    bool dns_enabled() const {return dns_enabled_;}
    uint32_t label() const {return label_;}
    bool IsL2LabelValid(uint32_t label) const { return (label_ == label);}
    uint32_t os_index() const {return os_index_;}
    const ether_addr &mac() const {return mac_;}

    static bool test_mode() {return test_mode_;}
    static void set_test_mode(bool mode) {test_mode_ = mode;}
protected:
    void SetItfSandeshData(ItfSandeshData &data) const;

    Type type_;
    boost::uuids::uuid uuid_;
    std::string name_;
    VrfEntryRef vrf_;
    uint32_t label_;
    uint32_t l2_label_;
    bool active_;
    size_t id_;
    bool dhcp_enabled_;
    bool dns_enabled_;
    struct ether_addr mac_;
    size_t os_index_;
    static bool test_mode_;

private:
    friend class InterfaceTable;
    InterfaceTable *table_;
    DISALLOW_COPY_AND_ASSIGN(Interface);
};

// Common key for all interfaces. 
struct InterfaceKey : public AgentKey {
    InterfaceKey(const InterfaceKey &rhs) {
        type_ = rhs.type_;
        uuid_ = rhs.uuid_;
        name_ = rhs.name_;
        is_mcast_nh_ = rhs.is_mcast_nh_;
    }

    InterfaceKey(Interface::Type type, const boost::uuids::uuid &uuid,
                 const std::string &name) : 
        AgentKey(), type_(type), uuid_(uuid), name_(name),
        is_mcast_nh_(false) { 
    }

    InterfaceKey(Interface::Type type, const boost::uuids::uuid &uuid,
                 const std::string &name, bool is_mcast) :
        AgentKey(), type_(type), uuid_(uuid), name_(name),
        is_mcast_nh_(is_mcast) {
    }

    InterfaceKey(AgentKey::DBSubOperation sub_op, Interface::Type type, 
                 const boost::uuids::uuid &uuid, const std::string &name) : 
        AgentKey(sub_op), type_(type), uuid_(uuid), name_(name) {
    }

    void Init(Interface::Type type, const boost::uuids::uuid &intf_uuid,
              const std::string &name) {
        type_ = type;
        uuid_ = intf_uuid;
        name_ = name;
        is_mcast_nh_ = false;
    }

    bool Compare(const InterfaceKey &rhs) const {
        if (type_ != rhs.type_)
            return false;

        if (uuid_ != rhs.uuid_)
            return false;

        if (name_ != rhs.name_)
            return false;

        return is_mcast_nh_ == rhs.is_mcast_nh_;
    }

    // Virtual methods for interface keys
    virtual Interface *AllocEntry() const = 0;
    virtual Interface *AllocEntry(const InterfaceData *data) const = 0;
    virtual InterfaceKey *Clone() const = 0;

    Interface::Type type_;
    boost::uuids::uuid uuid_;
    std::string name_;
    bool is_mcast_nh_;
};

// Common data for all interfaces. The data is further derived based on type
// of interfaces
struct InterfaceData : public AgentData {
    InterfaceData() : AgentData() { }

    void VmPortInit() { vrf_name_ = ""; }
    void EthInit(const std::string &vrf_name) { vrf_name_ = vrf_name; }
    void PktInit() { vrf_name_ = ""; }
    void VirtualHostInit(const std::string &vrf_name) { vrf_name_ = vrf_name; }

    // This is constant-data. Set only during create and not modified later
    std::string vrf_name_;
};

/////////////////////////////////////////////////////////////////////////////
// Interface Table
// Index for interface is maintained using an IndexVector.
/////////////////////////////////////////////////////////////////////////////
class InterfaceTable : public AgentDBTable {
public:
    InterfaceTable(DB *db, const std::string &name) :
        AgentDBTable(db, name), operdb_(NULL), agent_(NULL),
        walkid_(DBTableWalker::kInvalidWalkerId), index_table_() { 
    }
    virtual ~InterfaceTable() { }

    void Init(OperDB *oper);
    static DBTableBase *CreateTable(DB *db, const std::string &name);

    // DBTable virtual functions
    std::auto_ptr<DBEntry> AllocEntry(const DBRequestKey *k) const;
    size_t Hash(const DBEntry *entry) const { return 0; }
    size_t Hash(const DBRequestKey *key) const { return 0; }

    DBEntry *Add(const DBRequest *req);
    bool OnChange(DBEntry *entry, const DBRequest *req);
    void Delete(DBEntry *entry, const DBRequest *req);
    bool Resync(DBEntry *entry, DBRequest *req);

    // Config handlers
    bool IFNodeToReq(IFMapNode *node, DBRequest &req);
    // Handle change in config VRF for the interface
    static void VmInterfaceVrfSync(IFMapNode *node);
    // Handle change in VxLan Identifier mode from global-config
    void UpdateVxLanNetworkIdentifierMode();

    // Helper functions
    VrfEntry *FindVrfRef(const std::string &name) const;
    VnEntry *FindVnRef(const boost::uuids::uuid &uuid) const;
    VmEntry *FindVmRef(const boost::uuids::uuid &uuid) const;
    MirrorEntry *FindMirrorRef(const std::string &name) const;

    // Interface index managing routines
    void FreeInterfaceId(size_t index) { index_table_.Remove(index); }
    Interface *FindInterface(size_t index) { return index_table_.At(index); }
    Interface *FindInterfaceFromMetadataIp(const Ip4Address &ip);

    // Metadata address management routines
    bool FindVmUuidFromMetadataIp(const Ip4Address &ip, std::string *vm_ip,
                                  std::string *vm_uuid);
    void VmPortToMetaDataIp(uint16_t ifindex, uint32_t vrfid, Ip4Address *addr);

    // TODO : to remove this
    static InterfaceTable *GetInstance() { return interface_table_; }

private:
    bool VmInterfaceWalk(DBTablePartBase *partition, DBEntryBase *entry);
    void VmInterfaceWalkDone(DBTableBase *partition);

    static InterfaceTable *interface_table_;
    OperDB *operdb_;        // Cached entry
    Agent *agent_;          // Cached entry
    DBTableWalker::WalkId walkid_;
    IndexVector<Interface> index_table_;
    DISALLOW_COPY_AND_ASSIGN(InterfaceTable);
};

#endif // vnsw_agent_interface_hpp
