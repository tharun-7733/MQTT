// #include <iostream>
// #include <iomanip>
// #include "../protocol/packetSerializer.h"
// using namespace std;
// int main(){
//     mqtt::ConnackPacket connack;
//     connack.returnCode = 0;

//     auto bytes = mqtt::PacketSerializer::serializeConnack(connack);
//     cout << "Vector size: " << bytes.size() << endl;

//     for (auto b : bytes)
//         cout << setw(2) << setfill('0') << hex << (int)b << " ";

//     cout << endl;
// }