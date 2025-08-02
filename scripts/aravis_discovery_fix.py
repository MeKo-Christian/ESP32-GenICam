#!/usr/bin/env python3
"""
Aravis Discovery Fix - Disable localhost interface for GigE Vision discovery
This resolves the issue where Aravis tries to discover devices via localhost interface
"""

import subprocess
import os
import sys

def fix_aravis_discovery():
    """
    Set environment variable to exclude localhost from Aravis interface discovery
    """
    
    print("Aravis Discovery Fix")
    print("===================")
    print("Issue: Aravis tries to discover via localhost (127.0.0.1) interface")
    print("Solution: Exclude localhost from discovery interfaces")
    print()
    
    # Set ARV_GV_INTERFACE environment variable to exclude localhost
    # This tells Aravis to only use real network interfaces
    env = os.environ.copy()
    
    # Get real network interfaces (exclude localhost)
    real_interfaces = []
    try:
        # Get interface list
        result = subprocess.run(['ip', 'route', 'show'], capture_output=True, text=True)
        for line in result.stdout.split('\n'):
            if 'src' in line and 'dev' in line and '127.0.0.1' not in line:
                # Extract source IP
                parts = line.split()
                for i, part in enumerate(parts):
                    if part == 'src' and i + 1 < len(parts):
                        ip = parts[i + 1]
                        if ip not in real_interfaces and not ip.startswith('127.'):
                            real_interfaces.append(ip)
        
        if real_interfaces:
            # Set Aravis to use only real interfaces
            interface_list = ','.join(real_interfaces)
            env['ARV_GV_INTERFACE'] = interface_list
            print(f"✅ Configuring Aravis to use interfaces: {interface_list}")
        else:
            print("❌ No real network interfaces found")
            return False
            
    except Exception as e:
        print(f"❌ Error detecting interfaces: {e}")
        return False
    
    # Test discovery with fixed configuration
    print("\nTesting Aravis discovery with interface fix...")
    try:
        result = subprocess.run(['arv-test-0.8'], 
                              env=env, 
                              capture_output=True, 
                              text=True, 
                              timeout=10)
        
        print("Discovery Result:")
        print(result.stdout)
        
        if "Found 1 device" in result.stdout or "ESP32" in result.stdout:
            print("✅ SUCCESS: ESP32 device discovered!")
            print("\n💡 To make this permanent, add to your shell profile:")
            print(f"export ARV_GV_INTERFACE='{interface_list}'")
            return True
        else:
            print("❌ Device still not discovered")
            print("Debug output:")
            print(result.stderr)
            return False
            
    except subprocess.TimeoutExpired:
        print("❌ Discovery timed out")
        return False
    except Exception as e:
        print(f"❌ Error running discovery: {e}")
        return False

def main():
    if len(sys.argv) > 1 and sys.argv[1] == "--help":
        print("Usage: python3 aravis_discovery_fix.py")
        print("Fixes Aravis GigE Vision discovery by excluding localhost interface")
        print("\nThis resolves the issue where Aravis cannot discover ESP32-CAM devices")
        print("because it attempts discovery via the localhost interface (127.0.0.1)")
        return
    
    success = fix_aravis_discovery()
    sys.exit(0 if success else 1)

if __name__ == "__main__":
    main()