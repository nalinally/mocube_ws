#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <functional>

#include "Protocol.h"

/**
 * Wi-Fi 接続と、PC (device_manager) との UDP リンクを管理する。
 *
 * - ID 未取得の間は REGISTER を定期ブロードキャストし、"ID,<id>" で ID を得る
 * - HEARTBEAT を定期ブロードキャストし、"ACK,<id>" で PC 接続中と判断する
 * - ID / ACK 以外のコマンドは onCommand() で登録したハンドラへ振り分ける
 *
 * 新しい PC -> モジュール コマンドを追加するときは、機能側のクラスに
 * handleCommand(const protocol::Packet&) を作り main.cpp で onCommand() する。
 */
class PcLink {
public:
    using CommandHandler = std::function<void(const protocol::Packet&)>;

    // Wi-Fi に繋がるまでブロックする。待機中 500ms ごとに onWaiting を呼ぶ
    void connectWiFi(
        const char* ssid,
        const char* password,
        const std::function<void()>& onWaiting = nullptr);

    // UDP を開いて最初の REGISTER を送る (connectWiFi の後に呼ぶ)
    void begin(uint16_t port);

    // 受信1パケットの処理と REGISTER / HEARTBEAT / タイムアウト判定
    void update(unsigned long now);

    // Wi-Fi 切断時に呼ぶ。PC 接続状態を落とす
    void handleWiFiLost();

    void onCommand(const char* command, CommandHandler handler);

    void broadcast(const String& message);

    bool isWiFiConnected() const;
    bool isPcConnected() const { return _pcConnected; }
    int id() const { return _id; }  // 未取得なら -1
    const String& mac() const { return _mac; }

private:
    static constexpr int MAX_HANDLERS = 8;
    static constexpr size_t RX_BUFFER_SIZE = 256;

    struct HandlerEntry {
        const char* command = nullptr;
        CommandHandler handler;
    };

    WiFiUDP _udp;
    uint16_t _port = 0;

    String _mac;
    int _id = -1;
    bool _pcConnected = false;

    unsigned long _lastRegister = 0;
    unsigned long _lastHeartbeat = 0;
    unsigned long _lastPcAck = 0;

    HandlerEntry _handlers[MAX_HANDLERS];
    int _handlerCount = 0;

    IPAddress broadcastAddress() const;

    // 1パケット受信して処理。表示更新が必要なら true
    void receive();
    void dispatch(const protocol::Packet& packet);
};
