#include <iostream>
#include <string>
#include <thread>
#include <vector>
#include <list>
#include <sys/socket.h>
#include <netinet/in.h>
#include <zconf.h>
#include <arpa/inet.h>
#include <map>
#include <unordered_map>
#include <memory>

using std::cout;
using std::endl;
using std::string;
using std::vector;
using std::list;
using std::mutex;
using std::thread;
using std::lock_guard;
using std::map;
using std::unordered_map;
using std::shared_ptr;
using std::make_shared;

/*
 * 已连接的
 */
class AcceptedSocket {
public:
    AcceptedSocket(int _accepted_socket, const char *_client_ip, sockaddr_in _client_addr) : accepted_socket(
            _accepted_socket), client_ip(_client_ip), client_addr(_client_addr) {

    }

    ~AcceptedSocket() {
        if (accepted_socket < 0)return;
        close(accepted_socket);
        cout << "close:\n\t" << info() << endl;
    }

    AcceptedSocket(const AcceptedSocket &) = default;

    AcceptedSocket(AcceptedSocket &&) = default;

    AcceptedSocket &operator=(AcceptedSocket &&) = default;

    AcceptedSocket &operator=(const AcceptedSocket &) = default;

    string info() {
        string str;
        str += client_ip + ":" + std::to_string(ntohs(client_addr.sin_port));
        return str;
    }

    void show_info() {
        std::cout << "...connect " << info() << std::endl;
    }

    int receive_message(string &msg) {
        memset(buffer, 0, sizeof(buffer));
        try {
            int len = recv(accepted_socket, buffer, sizeof(buffer), 0);
            msg = buffer;
            return len;
        } catch (...) {
            cout << "Error: recv." << endl;
            msg.clear();
            return 0;
        }
    }

    void send_message(string &msg) const {
        if (accepted_socket < 0)return;
        try {
            send(accepted_socket, msg.c_str(), msg.size(), 0);
        } catch (...) {
            cout << "Error: send." << endl;
        }
    }

    bool valid() const {
        return accepted_socket >= 0;
    }

    int get_socket() const {
        return accepted_socket;
    }

    void do_close() const {
        if (valid())
            close(accepted_socket);
    }

private:
    int accepted_socket;
    string client_ip;
    sockaddr_in client_addr;
    constexpr static int buffer_size = 1024;
    char buffer[buffer_size];
};

/*
 * 监听的socket
 */
class ListenSocket {
public:
    explicit ListenSocket(ushort _port, int _sin_family = AF_INET, int _sin_type = SOCK_STREAM, int _addr = INADDR_ANY,
                          int _backlog = 128)
            : sin_family(_sin_family),
              sin_type(_sin_type), port(_port),
              addr(_addr), backlog(_backlog), listen_fd(-1) {}

    ~ListenSocket() = default;

    ListenSocket(const ListenSocket &) = default;

    ListenSocket(ListenSocket &&) = default;

    ListenSocket &operator=(const ListenSocket &) = default;

    ListenSocket &operator=(ListenSocket &&) = default;

    void init();

private:
    // 广播（读）；删除，增加（写）
    static unordered_map<int, shared_ptr<AcceptedSocket>> accept_sockets;
    static mutex mtx;

    int listen_fd; // listen file descriptor
    int sin_family; // AF_INET
    int sin_type; // 0, 根据family自动选择
    ushort port; // 端口
    int addr; // ip
    int backlog;

    static void show_tip(string &tip) {
        cout << tip << endl;
    }

    void show_listen_success() const {
        auto str = static_cast<string>("server is listening port: ");
        str += std::to_string(port);
        show_tip(str);
    }

    static void show_server_init() {
        auto str = string("server init...");
        show_tip(str);
    }

    void start_thread(AcceptedSocket &conn) {
        thread t([&conn, this] {
                     if (!conn.valid())return;
                     while (true) {
                         string msg;
                         try {
                             if (conn.receive_message(msg) == 0) {
                                 accept_sockets.erase(conn.get_socket());
                                 break;
                             }
                         } catch (...) {
                             accept_sockets.erase(conn.get_socket());
                             break;
                         }

                         cout << msg << endl;

                         {
                             lock_guard<mutex> lockGuard(mtx);
                             cout << "sockets_size: " << accept_sockets.size() << endl;
                             for (auto it = accept_sockets.begin(); it != accept_sockets.end();) {
                                 try {
                                     if (it->first != conn.get_socket())
                                         it->second->send_message(msg);
                                 } catch (...) {
                                     it = accept_sockets.erase(it);
                                     continue;
                                 }
                                 it++;
                             }
                         }
                     }
                 }
        );
        t.detach();
    }

};

unordered_map<int, shared_ptr<AcceptedSocket>> ListenSocket::accept_sockets;
mutex ListenSocket::mtx;

void ListenSocket::init() {
    show_server_init();
    // socket
    listen_fd = socket(sin_family, sin_type, 0);
    if (listen_fd == -1) {
        std::cout << "Error: socket" << std::endl;
        return;
    }
    // bind
    sockaddr_in addr_in;
    addr_in.sin_family = sin_family;
    addr_in.sin_addr.s_addr = addr;
    addr_in.sin_port = htons(port);
    if (bind(listen_fd, reinterpret_cast<sockaddr *>(&addr_in), sizeof(addr_in)) == -1) {
        std::cout << "Error: bind" << std::endl;
        return;
    }
    // listen
    if (listen(listen_fd, backlog) == -1) {
        std::cout << "Error: listen" << std::endl;
        return;
    }
    show_listen_success();
    char clientIP[INET_ADDRSTRLEN] = "";
    sockaddr_in clientAddr;
    socklen_t clientAddrLen = sizeof(clientAddr);
    int conn;
    while (true) {
        conn = accept(listen_fd, (sockaddr *) &clientAddr, &clientAddrLen);
        if (conn < 0) {
            std::cout << "Error: accept" << std::endl;
            continue;
        }
        inet_ntop(AF_INET, &clientAddr.sin_addr, clientIP, INET_ADDRSTRLEN);
        shared_ptr<AcceptedSocket> new_socket = make_shared<AcceptedSocket>(conn, clientIP, clientAddr);
        new_socket->show_info();
        start_thread(*new_socket);
        {
            lock_guard<mutex> lockGuard(mtx);
            accept_sockets[new_socket->get_socket()] = new_socket;
        }
    }
}

int main() {
    ListenSocket listenSocket(9000);
    listenSocket.init();
    return 0;
}
