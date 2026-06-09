#ifndef NET
#define NET

#include <string.h>
#include <map>
#include <omnetpp.h>
#include <packet_m.h>

using namespace omnetpp;

class Net: public cSimpleModule {
private:
    // ruta aprendida del mejor camino para llegar a un destino dado
    struct Route {
        int outGate;   // toLnk[], por que interfaz salir (0,1) en este caso
        int hopCount;  // saltos que hay que dar para llegar
    };

    std::map<int, Route> routingTable;  // dict que relaciona numeros(nodos) con Route's
    cMessage *helloTimer;
    simtime_t helloInterval;
    bool UseShortestPath; // false -> usar idea del kick, true -> usar nuestro algo
    int myId; // simplificacion del codigo
    int numIfaces; // punto *

    void broadcastHello();
    void learnRoute(int destId, int viaGate, int hops);
public:
    Net();
    virtual ~Net();
protected:
    virtual void initialize();
    virtual void finish();
    virtual void handleMessage(cMessage *msg);
};

Define_Module(Net);

#endif /* NET */

Net::Net() {
    helloTimer = NULL;
}

Net::~Net() {
    cancelAndDelete(helloTimer);
}

void Net::initialize() {
    myId = getParentModule()->getIndex();
    numIfaces = gateSize("toLnk$o");
    helloInterval = par("helloInterval");
    UseShortestPath = par("useShortestPath");

    // Descubrimiento de topología: cada nodo inunda la red con paquetes HELLO 
    // por todas sus interfaces. A medida que viajan, cada nodo aprende qué tan
    // lejos está cada destino y por qué interfaz. Se reenvían periódicamente
    // para mantener la tabla actualizada.
    // El kickstarter no usa esto, por lo que no genera tráfico de control.
    if (UseShortestPath) {
        helloTimer = new cMessage("helloTimer");
        scheduleAt(simTime() + uniform(0, 1), helloTimer);
    }
}

void Net::finish() {
}

void Net::broadcastHello() {
    for (int g = 0; g < numIfaces; g++) {
        Packet *hello = new Packet("hello");
        // getters y setters de packet definidos en packet_m.cc
        hello->setIsHello(true);
        hello->setByteLength(64);     // paquete de control => chico
        hello->setSource(myId);       // Asigno id del nodo al paquete
        hello->setDestination(-1);    // unused for control traffic
        hello->setHopCount(1);        // distancia recorrida, se acaba de crear
                                      // cuando llegue al vecino, recorrio 1  
        send(hello, "toLnk$o", g);
    }
}

void Net::learnRoute(int destId, int viaGate, int hops) {
    auto it = routingTable.find(destId);
    // keep the shortest of the (at most two, in a ring) paths we hear about
    if (it == routingTable.end() || hops < it->second.hopCount) {
        routingTable[destId] = Route{viaGate, hops};
    }
}

void Net::handleMessage(cMessage *msg) {

    // MENSAJE INTERNO: enviar paquetes hello
    if (msg == helloTimer) {
        broadcastHello();
        scheduleAt(simTime() + helloInterval, helloTimer);
        return;
    }

    Packet *pkt = (Packet *) msg;

    // MENSAJE EXTERNO: llega paquete hello
    if (pkt->isHello()) {
        int arrivalGate = pkt->getArrivalGate()->getIndex();
        int originId = pkt->getSource();
        int hops = pkt->getHopCount();

        // veo si el paquete es el que yo habia enviado y ya regreso
        if (originId == myId) {
            delete msg;
            return;
        }

        // contiene la logica de decidir y reaccionar en base a si el paquete 
        // hello tiene mejor ruta a un destino conocido o si nos esta 
        // informando de una ruta desconocida para nos
        learnRoute(originId, arrivalGate, hops);
        
        // incremento los pasos del paquete y envio por la misma direccion en 
        // la que venia
        pkt->setHopCount(hops + 1);
        send(pkt, "toLnk$o", 1 - arrivalGate);
        return;
    }

    // ---- regular data packet -------------------------------------------

    // si soy el destino final, envio el msj a la capa de aplicacion
    if (pkt->getDestination() == myId) {
        send(msg, "toApp$o");
        return;
    }

    // Por defecto puerta 0 (kickstarter). Si el enrutamiento adaptativo
    // está activo y existe una ruta aprendida, usar el camino más corto.
    pkt->setHopCount(pkt->getHopCount() + 1);

    int outGate = 0; 

    if (UseShortestPath) {

        auto it = routingTable.find(pkt->getDestination());
        if (it != routingTable.end())
            outGate = it->second.outGate;
    }

    send(msg, "toLnk$o", outGate);
}
