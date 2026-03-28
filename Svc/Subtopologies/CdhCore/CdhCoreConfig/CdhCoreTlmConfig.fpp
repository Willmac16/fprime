module CdhCore{

    # Uncomment the following block and comment the TlmPacketizer block to use TlmChan instead
    # instance tlmSend: Svc.TlmChan base id CdhCoreConfig.BASE_ID + 0x06000 \
    #     queue size CdhCoreConfig.QueueSizes.tlmSend \
    #     stack size CdhCoreConfig.StackSizes.tlmSend \
    #     priority CdhCoreConfig.Priorities.tlmSend \

    instance tlmSend: Svc.TlmPacketizer base id CdhCoreConfig.BASE_ID + 0x06000 \
       queue size CdhCoreConfig.QueueSizes.tlmSend \
       stack size CdhCoreConfig.StackSizes.tlmSend \
       priority CdhCoreConfig.Priorities.tlmSend \
    {
       # NOTE: The Name Ref is specific to the Reference deployment, Ref
       # This name will need to be updated if wishing to use this in a custom deployment
       phase Fpp.ToCpp.Phases.configComponents """
       CdhCore::tlmSend.setPacketList(
           Ref::Ref_RefPacketsTlmPackets::packetList,
           Ref::Ref_RefPacketsTlmPackets::omittedChannels,
           1
       );
       """
    }
}
