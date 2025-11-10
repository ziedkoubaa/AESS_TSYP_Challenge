#!/usr/bin/env python3
import RPi.GPIO as GPIO
import time
import logging
import os

# GPIO setup
HEARTBEAT_PIN = 17    # GPIO17 -> ESP32 D25
RESET_PIN = 27        # GPIO27 <- ESP32 D26 (reset signal)
STATUS_LED = 18       # GPIO18 for status LED (optional)

# Setup logging
logging.basicConfig(
    level=logging.INFO,
    format='%(asctime)s - %(levelname)s - %(message)s',
    handlers=[
        logging.FileHandler('/var/log/heartbeat.log'),
        logging.StreamHandler()
    ]
)

def setup_gpio():
    GPIO.setmode(GPIO.BCM)
    GPIO.setup(HEARTBEAT_PIN, GPIO.OUT)
    GPIO.setup(RESET_PIN, GPIO.IN, pull_up_down=GPIO.PUD_UP)
    GPIO.setup(STATUS_LED, GPIO.OUT)
    
    GPIO.output(HEARTBEAT_PIN, GPIO.HIGH)
    GPIO.output(STATUS_LED, GPIO.LOW)

def send_heartbeat():
    """Send heartbeat pulse to ESP32"""
    GPIO.output(HEARTBEAT_PIN, GPIO.LOW)
    time.sleep(0.01)  # 10ms pulse
    GPIO.output(HEARTBEAT_PIN, GPIO.HIGH)

def blink_led():
    """Toggle status LED"""
    GPIO.output(STATUS_LED, not GPIO.input(STATUS_LED))

def check_reset_signal():
    """Check if ESP32 is sending reset signal"""
    return GPIO.input(RESET_PIN) == GPIO.LOW

def graceful_shutdown():
    """Perform graceful shutdown when reset signal is received"""
    logging.info("Reset signal received from ESP32! Performing graceful shutdown...")
    
    # Turn LED solid ON to indicate shutdown
    GPIO.output(STATUS_LED, GPIO.HIGH)
    
    # Perform shutdown and reboot
    os.system("sudo shutdown -r now")

def main():
    setup_gpio()
    logging.info("Raspberry Pi Heartbeat Started")
    
    # Blink LED quickly at startup
    for i in range(5):
        GPIO.output(STATUS_LED, GPIO.HIGH)
        time.sleep(0.2)
        GPIO.output(STATUS_LED, GPIO.LOW)
        time.sleep(0.2)
    
    heartbeat_count = 0
    last_status_time = time.time()
    
    try:
        while True:
            # Send heartbeat every 2 seconds
            send_heartbeat()
            heartbeat_count += 1
            
            # Blink LED during normal operation
            blink_led()
            
            # Check for reset signal from ESP32
            if check_reset_signal():
                graceful_shutdown()
                break
            
            # Print status every 30 seconds
            if time.time() - last_status_time > 30:
                logging.info(f"Heartbeats sent: {heartbeat_count}")
                last_status_time = time.time()
            
            time.sleep(1.99)  # ~2 second cycle
            
    except KeyboardInterrupt:
        logging.info("Heartbeat monitor stopped by user")
        GPIO.output(STATUS_LED, GPIO.LOW)
    except Exception as e:
        logging.error(f"Error in heartbeat monitor: {e}")
    finally:
        GPIO.cleanup()

if __name__ == "__main__":
    main()