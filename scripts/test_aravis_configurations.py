#!/usr/bin/env python3
"""
Test different Aravis configurations for ESP32 discovery
This script tests various environment variables and settings to see if we can
make Aravis discover ESP32 devices directly without the proxy.
"""

import os
import subprocess
import time
import sys
from datetime import datetime

class AravisConfigTester:
    def __init__(self, esp32_ip="192.168.213.40"):
        self.esp32_ip = esp32_ip
        self.test_results = []
        
    def log(self, message):
        """Log a message with timestamp"""
        timestamp = datetime.now().strftime('%H:%M:%S.%f')[:-3]
        print(f"[{timestamp}] {message}")
        
    def run_aravis_test(self, env_vars=None, test_name="Default"):
        """Run Aravis discovery test with specific environment variables"""
        self.log(f"Starting test: {test_name}")
        
        # Set up environment
        env = os.environ.copy()
        if env_vars:
            for key, value in env_vars.items():
                env[key] = str(value)
                self.log(f"  Setting {key}={value}")
        
        # Run arv-test with timeout
        try:
            start_time = time.time()
            result = subprocess.run(
                ['arv-test-0.10'],
                env=env,
                capture_output=True,
                text=True,
                timeout=10  # 10 second timeout
            )
            end_time = time.time()
            
            # Analyze output
            output = result.stdout + result.stderr
            discovered_devices = []
            
            # Look for device discoveries
            lines = output.split('\n')
            for line in lines:
                if self.esp32_ip in line or "ESP32" in line or "GenICam" in line:
                    discovered_devices.append(line.strip())
            
            test_result = {
                "test_name": test_name,
                "env_vars": env_vars or {},
                "return_code": result.returncode,
                "duration": end_time - start_time,
                "discovered_devices": discovered_devices,
                "full_output": output[:500],  # First 500 chars
                "success": len(discovered_devices) > 0
            }
            
            if discovered_devices:
                self.log(f"  ✅ SUCCESS: Found {len(discovered_devices)} devices")
                for device in discovered_devices:
                    self.log(f"    Device: {device}")
            else:
                self.log(f"  ❌ FAILED: No ESP32 devices discovered")
                
            self.test_results.append(test_result)
            return test_result
            
        except subprocess.TimeoutExpired:
            self.log(f"  ⏰ TIMEOUT: Test timed out after 10 seconds")
            test_result = {
                "test_name": test_name,
                "env_vars": env_vars or {},
                "return_code": -1,
                "duration": 10.0,
                "discovered_devices": [],
                "full_output": "TIMEOUT",
                "success": False
            }
            self.test_results.append(test_result)
            return test_result
            
        except FileNotFoundError:
            self.log(f"  ❌ ERROR: arv-test-0.10 not found. Install with: sudo apt install aravis-tools")
            return None
    
    def test_basic_discovery(self):
        """Test 1: Basic discovery (no special settings)"""
        return self.run_aravis_test(test_name="Basic Discovery")
    
    def test_debug_discovery(self):
        """Test 2: Discovery with debug output"""
        return self.run_aravis_test(
            env_vars={"ARV_DEBUG": "all"},
            test_name="Debug Discovery"
        )
    
    def test_interface_specific(self):
        """Test 3: Bind to specific interfaces"""
        test_results = []
        
        # Test with specific interface IPs
        interfaces = [
            "192.168.213.45",  # Ethernet interface
            "192.168.213.28"   # WiFi interface
        ]
        
        for interface_ip in interfaces:
            result = self.run_aravis_test(
                env_vars={"ARV_GVCP_SOCKET_BIND_IP": interface_ip},
                test_name=f"Interface {interface_ip}"
            )
            if result:
                test_results.append(result)
                
        return test_results
    
    def test_discovery_timeout(self):
        """Test 4: Different discovery timeouts"""
        test_results = []
        
        timeouts = [1000, 3000, 5000, 10000]  # milliseconds
        
        for timeout in timeouts:
            result = self.run_aravis_test(
                env_vars={"ARV_DISCOVERY_TIMEOUT": timeout},
                test_name=f"Timeout {timeout}ms"
            )
            if result:
                test_results.append(result)
                
        return test_results
    
    def test_packet_socket_settings(self):
        """Test 5: Different packet socket settings"""
        test_results = []
        
        # Test different socket settings
        socket_configs = [
            {"ARV_PACKET_SOCKET_ENABLE": "0"},
            {"ARV_PACKET_SOCKET_ENABLE": "1"},
            {"ARV_FAKE_CAMERA": "TEST"},
        ]
        
        for i, config in enumerate(socket_configs):
            result = self.run_aravis_test(
                env_vars=config,
                test_name=f"Socket Config {i+1}"
            )
            if result:
                test_results.append(result)
                
        return test_results
    
    def test_network_settings(self):
        """Test 6: Network-specific settings"""
        test_results = []
        
        # Test network configurations
        network_configs = [
            {"ARV_GVCP_SOCKET_BIND_ADDRESS": "0.0.0.0"},
            {"ARV_GVCP_SOCKET_BIND_ADDRESS": "192.168.213.45"},
            {"ARV_GVCP_SOCKET_BIND_ADDRESS": "192.168.213.28"},
            {"ARV_AUTO_SOCKET_BUFFER": "0"},
            {"ARV_AUTO_SOCKET_BUFFER": "1"},
        ]
        
        for i, config in enumerate(network_configs):
            result = self.run_aravis_test(
                env_vars=config,
                test_name=f"Network Config {i+1}"
            )
            if result:
                test_results.append(result)
                
        return test_results
    
    def run_all_tests(self):
        """Run all Aravis configuration tests"""
        self.log("=== ARAVIS CONFIGURATION TESTING ===")
        self.log(f"Testing ESP32 discovery at {self.esp32_ip}")
        self.log("")
        
        # Run all test categories
        self.test_basic_discovery()
        time.sleep(2)
        
        self.test_debug_discovery()
        time.sleep(2)
        
        self.test_interface_specific()
        time.sleep(2)
        
        self.test_discovery_timeout()
        time.sleep(2)
        
        self.test_packet_socket_settings()
        time.sleep(2)
        
        self.test_network_settings()
        
        # Print summary
        self.print_summary()
        
    def print_summary(self):
        """Print test results summary"""
        self.log("")
        self.log("=" * 60)
        self.log("TEST RESULTS SUMMARY")
        self.log("=" * 60)
        
        successful_tests = [t for t in self.test_results if t['success']]
        failed_tests = [t for t in self.test_results if not t['success']]
        
        self.log(f"Total tests: {len(self.test_results)}")
        self.log(f"Successful: {len(successful_tests)}")
        self.log(f"Failed: {len(failed_tests)}")
        self.log("")
        
        if successful_tests:
            self.log("✅ SUCCESSFUL CONFIGURATIONS:")
            for test in successful_tests:
                self.log(f"  - {test['test_name']}")
                if test['env_vars']:
                    for key, value in test['env_vars'].items():
                        self.log(f"    {key}={value}")
                self.log(f"    Devices: {len(test['discovered_devices'])}")
                for device in test['discovered_devices']:
                    self.log(f"      {device}")
                self.log("")
        
        if failed_tests:
            self.log("❌ FAILED CONFIGURATIONS:")
            for test in failed_tests:
                self.log(f"  - {test['test_name']}")
                if test['env_vars']:
                    for key, value in test['env_vars'].items():
                        self.log(f"    {key}={value}")
                self.log("")
        
        # Recommendations
        self.log("=" * 60)
        self.log("RECOMMENDATIONS")
        self.log("=" * 60)
        
        if successful_tests:
            self.log("✅ ESP32 can be discovered with some configurations!")
            self.log("   Use the successful environment variables above.")
        else:
            self.log("❌ No configurations worked for direct ESP32 discovery")
            self.log("   This confirms that the discovery proxy is necessary")
            self.log("   for reliable Aravis operation with ESP32-CAM.")
            
        self.log("")
        self.log("Next steps:")
        self.log("1. If successful configs found: Document and automate them")
        self.log("2. If no success: Investigate Aravis source code for validation logic")
        self.log("3. Compare with other GigE Vision clients (Spinnaker, Vimba)")

def main():
    if len(sys.argv) > 1:
        esp32_ip = sys.argv[1]
    else:
        esp32_ip = "192.168.213.40"  # Default from PLAN.md
        
    print(f"ESP32 IP: {esp32_ip}")
    print("Make sure ESP32-CAM is flashed and running!")
    print("Press Enter to continue or Ctrl+C to abort...")
    input()
    
    tester = AravisConfigTester(esp32_ip)
    tester.run_all_tests()

if __name__ == "__main__":
    main()