packet Packet {
    int source;
    int destination;
    int hopCount;
    bool isHello = false; // true for routing-control HELLO packets, false for user data
}
