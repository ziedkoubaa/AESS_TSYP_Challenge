#!/usr/bin/env python3
import time
import threading
import random

class VirtualGPIOSystem:
    def __init__(self):
        self.heartbeat_line = False
        self.reset_line = False
        self.esp32_led = False
        
    def pi_send_heartbeat(self):
        """Raspberry Pi sends heartbeat pulse"""
        self.heartbeat_line = True
        time.sleep(0.01)  # 10ms pulse
        self.heartbeat_line = False
        
    def esp32_detect_heartbeat(self):
        """ESP32 detects heartbeat"""
        return self.heartbeat_line
    
    def esp32_send_reset(self):
        """ESP32 sends reset signal"""
        self.reset_line = True
        time.sleep(1)  # 1 second reset pulse
        self.reset_line = False
        
    def pi_detect_reset(self):
        """Raspberry Pi detects reset signal"""
        return self.reset_line

class ESP32Simulator:
    def __init__(self, gpio_system):
        self.gpio = gpio_system
        self.last_heartbeat = time.time()
        self.timeout = 10  # seconds
        self.pi_running = False
        self.led_state = False
        
    def run(self):
        print("ESP32: Watchdog started - Monitoring Raspberry Pi")
        
        while True:
            current_time = time.time()
            
            # Check for heartbeat
            if self.gpio.esp32_detect_heartbeat():
                self.last_heartbeat = current_time
                self.pi_running = True
                print("ESP32: Heartbeat detected")
                
            # Check for timeout
            if current_time - self.last_heartbeat > self.timeout:
                if self.pi_running:
                    print("ESP32: *** WATCHDOG TIMEOUT - RESETTING RASPBERRY PI ***")
                    self.gpio.esp32_send_reset()
                    self.pi_running = False
                    self.last_heartbeat = current_time
                    print("ESP32: Reset completed")
                    
            # Blink LED
            if int(current_time) % 2 == 0:
                self.led_state = not self.led_state
                
            time.sleep(0.1)

class RaspberryPiSimulator:
    def __init__(self, gpio_system):
        self.gpio = gpio_system
        self.heartbeat_count = 0
        self.is_running = True
        self.simulate_hang = False
        
    def run(self):
        print("Raspberry Pi: System started - Sending heartbeats")
        
        while self.is_running:
            if not self.simulate_hang:
                # Normal operation - send heartbeat
                self.gpio.pi_send_heartbeat()
                self.heartbeat_count += 1
                
                # Print status occasionally
                if self.heartbeat_count % 5 == 0:
                    print(f"Raspberry Pi: Heartbeat #{self.heartbeat_count}")
                    
            else:
                # Hang simulation - no heartbeats
                print("Raspberry Pi: *** SIMULATING HANG - No heartbeats ***")
                time.sleep(15)
                self.simulate_hang = False
                print("Raspberry Pi: Resuming normal operation")
                
            # Check for reset signal
            if self.gpio.pi_detect_reset():
                print("Raspberry Pi: *** RESET SIGNAL RECEIVED - Rebooting ***")
                time.sleep(3)  # Simulate reboot time
                self.heartbeat_count = 0
                print("Raspberry Pi: System rebooted")
                
            time.sleep(2)  # 2 second cycle

def main():
    print("=" * 60)
    print("ESP32 + Raspberry Pi Watchdog Simulation")
    print("=" * 60)
    
    # Create virtual GPIO system
    gpio_system = VirtualGPIOSystem()
    
    # Create simulators
    esp32 = ESP32Simulator(gpio_system)
    raspberry_pi = RaspberryPiSimulator(gpio_system)
    
    # Start simulators in separate threads
    esp32_thread = threading.Thread(target=esp32.run, daemon=True)
    pi_thread = threading.Thread(target=raspberry_pi.run, daemon=True)
    
    esp32_thread.start()
    pi_thread.start()
    
    # Manual control interface
    try:
        while True:
            print("\nCommands:")
            print("1 - Simulate Raspberry Pi hang")
            print("2 - Show status") 
            print("3 - Quit")
            print("4 - Auto-test sequence")
            
            choice = input("Enter choice: ").strip()
            
            if choice == "1":
                raspberry_pi.simulate_hang = True
                print("Manual hang simulation triggered!")
            elif choice == "2":
                status = "HANGING" if raspberry_pi.simulate_hang else "RUNNING"
                print(f"Raspberry Pi: {status}, Heartbeats: {raspberry_pi.heartbeat_count}")
                print(f"ESP32: Pi Running: {esp32.pi_running}")
            elif choice == "3":
                raspberry_pi.is_running = False
                break
            elif choice == "4":
                print("\n*** Starting auto-test sequence ***")
                print("Normal operation for 10 seconds...")
                time.sleep(10)
                print("Simulating hang for 15 seconds...")
                raspberry_pi.simulate_hang = True
                time.sleep(20)  # Wait for reset and recovery
                print("Auto-test completed!")
                
    except KeyboardInterrupt:
        raspberry_pi.is_running = False
        print("\nSimulation stopped")

if __name__ == "__main__":
    main()