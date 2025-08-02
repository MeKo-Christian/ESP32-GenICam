#!/usr/bin/env python3
"""
Quick Aravis discovery test to understand current behavior
"""

import subprocess
import time
import os

def test_aravis_discovery():
    """Test basic Aravis discovery"""
    print("Testing Aravis discovery without proxy...")
    print("This will help us understand current behavior patterns")
    print()
    
    try:
        # Test 1: Basic discovery
        print("=== Test 1: Basic Aravis Discovery ===")
        result = subprocess.run(
            ['arv-test-0.10'],
            capture_output=True,
            text=True,
            timeout=8
        )
        
        print("Return code:", result.returncode)
        print("Output:")
        print(result.stdout)
        if result.stderr:
            print("Errors:")
            print(result.stderr)
        
        print()
        
        # Test 2: Debug discovery
        print("=== Test 2: Aravis Discovery with Debug ===")
        env = os.environ.copy()
        env['ARV_DEBUG'] = 'all'
        
        result_debug = subprocess.run(
            ['arv-test-0.10'],
            env=env,
            capture_output=True,
            text=True,
            timeout=8
        )
        
        print("Return code:", result_debug.returncode)
        print("Debug output (first 1000 chars):")
        output = result_debug.stdout + result_debug.stderr
        print(output[:1000])
        if len(output) > 1000:
            print("... (truncated)")
        
        return True
        
    except subprocess.TimeoutExpired:
        print("Test timed out after 8 seconds")
        return False
    except FileNotFoundError:
        print("arv-test-0.10 not found")
        return False

if __name__ == "__main__":
    print("Quick Aravis Discovery Test")
    print("===========================")
    print()
    print("NOTE: This test is designed to show current Aravis behavior")
    print("without an ESP32 device. It will likely show 'no devices found'")
    print("but the debug output will reveal Aravis's discovery process.")
    print()
    
    test_aravis_discovery()
    
    print()
    print("Analysis:")
    print("- If devices are found: Great! Note what they are")
    print("- If no devices: This is expected without ESP32")
    print("- Check debug output for discovery packet patterns")
    print("- Look for interface binding and broadcast behavior")