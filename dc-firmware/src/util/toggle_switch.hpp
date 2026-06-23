#pragma once
#include <Arduino.h>

#define SWITCH_DURATION 100 // ms

class ToggleSwitch // デバウンス付きの2値入力。極性(onState)と入力モードを指定できる。
{
public:
    // 入力ピンの構成。OUTPUT 等を誤って渡せないよう入力系のみに限定する。
    enum class InputMode : uint8_t {
        PullUp,   // INPUT_PULLUP   (内部プルアップ)
        PullDown, // INPUT_PULLDOWN (内部プルダウン)
        External, // INPUT          (外部でプルしている前提の素の入力)
    };

    // プルアップ前提 (通常HIGH、押下=GND=LOW): onState=LOW, INPUT_PULLUP。
    ToggleSwitch(uint8_t pin);
    // 極性/入力モード明示。例: アクティブHIGH+外部PULLDOWN
    //   → ToggleSwitch(pin, HIGH, InputMode::External)。
    ToggleSwitch(uint8_t pin, uint8_t onState, InputMode inputMode);
    void initialize();
    void read();
    bool isOn() const;
    bool switched() const;
    bool switchedToOff() const;
    bool switchedToOn() const;

private:
    const uint8_t pin;
    const uint32_t duration = SWITCH_DURATION;
    const uint8_t onState;        // この値のとき「ON」とみなす
    const uint8_t mode;           // initialize() で設定する pinMode
    bool _isOn = false;
    bool _switched = false;
    uint8_t state = 0;            // current button state (initialize() で実値に上書き)
    uint8_t lastState = 0;        // previous button state
    uint32_t time = 0;            // time of current state (all times are in ms)
    uint32_t lastTime = 0;        // time of previous state
    uint32_t lastChange = 0;      // time of last state change
};

class SelectSwitch3Pin
{
public:
    enum class Status
    {
        Zero,
        First,
        Second,
        Third
    };
    SelectSwitch3Pin(uint8_t pin1, uint8_t pin2, uint8_t pin3);
    void initialize();
    void read();
    bool changed();
    Status getStatus();

private:
    ToggleSwitch toggleSwitch1, toggleSwitch2, toggleSwitch3;
};

