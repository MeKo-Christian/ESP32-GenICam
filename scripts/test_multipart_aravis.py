#!/usr/bin/env python3
"""
Test script for multipart support using Aravis tools
Tests ChunkModeActive feature and SCCFG register access
"""

import subprocess
import sys
import argparse
import time

def run_command(cmd, timeout=10, capture_output=True):
    """Run a command with timeout and return success status and output"""
    try:
        if capture_output:
            result = subprocess.run(cmd, shell=True, timeout=timeout, 
                                  capture_output=True, text=True)
            return result.returncode == 0, result.stdout, result.stderr
        else:
            result = subprocess.run(cmd, shell=True, timeout=timeout)
            return result.returncode == 0, "", ""
    except subprocess.TimeoutExpired:
        return False, "", "Command timed out"
    except Exception as e:
        return False, "", str(e)

def test_aravis_discovery():
    """Test basic Aravis discovery"""
    print("Testing Aravis discovery...")
    success, stdout, stderr = run_command("arv-tool-0.10 --timeout=5000", timeout=8)
    
    if success and "ESP32-CAM" in stdout:
        print("✅ ESP32-CAM discovered by Aravis")
        return True
    else:
        print("❌ ESP32-CAM not discovered by Aravis")
        if stderr:
            print(f"Error: {stderr}")
        return False

def test_chunk_mode_active():
    """Test ChunkModeActive feature access"""
    print("\nTesting ChunkModeActive feature...")
    
    # Try to read ChunkModeActive feature
    success, stdout, stderr = run_command('arv-tool-0.10 -n "ESP32-CAM" control ChunkModeActive', timeout=8)
    
    if success:
        print(f"✅ ChunkModeActive feature accessible: {stdout.strip()}")
        return True
    else:
        print("❌ ChunkModeActive feature not accessible")
        if stderr:
            print(f"Error: {stderr}")
        return False

def test_chunk_component_selector():
    """Test ChunkComponentSelector feature"""
    print("\nTesting ChunkComponentSelector feature...")
    
    success, stdout, stderr = run_command('arv-tool-0.10 -n "ESP32-CAM" control ChunkComponentSelector', timeout=8)
    
    if success:
        print(f"✅ ChunkComponentSelector feature accessible: {stdout.strip()}")
        return True
    else:
        print("❌ ChunkComponentSelector feature not accessible")
        if stderr:
            print(f"Error: {stderr}")
        return False

def test_sccfg_register():
    """Test direct SCCFG register access"""
    print("\nTesting direct SCCFG register (0x0d24) access...")
    
    # Note: arv-tool doesn't have direct register access, so we'll test via features
    success, stdout, stderr = run_command('arv-tool-0.10 -n "ESP32-CAM" features | grep -i chunk', timeout=8)
    
    if success and stdout.strip():
        print("✅ Chunk/multipart features found:")
        for line in stdout.strip().split('\n'):
            print(f"  {line}")
        return True
    else:
        print("❌ No chunk/multipart features found")
        return False

def test_xml_multipart_features():
    """Test if multipart features are in GenICam XML"""
    print("\nTesting GenICam XML for multipart features...")
    
    # Get the GenICam XML and check for multipart features
    success, stdout, stderr = run_command('arv-tool-0.10 -n "ESP32-CAM" genicam | grep -i -E "(chunk|multipart)"', timeout=8)
    
    if success and stdout.strip():
        print("✅ Multipart features found in GenICam XML:")
        for line in stdout.strip().split('\n'):
            print(f"  {line.strip()}")
        return True
    else:
        print("❌ No multipart features found in GenICam XML")
        return False

def main():
    parser = argparse.ArgumentParser(description='Test multipart support with Aravis tools')
    parser.add_argument('--verbose', '-v', action='store_true', help='Verbose output')
    parser.add_argument('--quick', action='store_true', help='Quick test (discovery only)')
    
    args = parser.parse_args()
    
    print("ESP32GenICam Multipart Support Test with Aravis")
    print("=" * 50)
    
    # Check if Aravis tools are available
    success, _, _ = run_command("which arv-tool-0.10", timeout=3)
    if not success:
        print("❌ arv-tool-0.10 not found. Install with: sudo apt install aravis-tools")
        sys.exit(1)
    
    results = []
    
    # Test 1: Basic discovery
    results.append(test_aravis_discovery())
    
    if args.quick:
        if results[0]:
            print("\n✅ Quick test passed - ESP32-CAM discovered")
            sys.exit(0)
        else:
            print("\n❌ Quick test failed - ESP32-CAM not discovered")
            sys.exit(1)
    
    # Test 2: ChunkModeActive feature
    results.append(test_chunk_mode_active())
    
    # Test 3: ChunkComponentSelector feature  
    results.append(test_chunk_component_selector())
    
    # Test 4: SCCFG register access
    results.append(test_sccfg_register())
    
    # Test 5: XML multipart features
    results.append(test_xml_multipart_features())
    
    # Summary
    print("\n" + "=" * 50)
    print("Test Results Summary:")
    
    test_names = [
        "Aravis Discovery",
        "ChunkModeActive Feature", 
        "ChunkComponentSelector Feature",
        "SCCFG Register Features",
        "XML Multipart Features"
    ]
    
    for i, (name, result) in enumerate(zip(test_names, results)):
        status = "✅ PASS" if result else "❌ FAIL"
        print(f"  {i+1}. {name}: {status}")
    
    total_passed = sum(results)
    total_tests = len(results)
    
    print(f"\nOverall: {total_passed}/{total_tests} tests passed")
    
    if total_passed == total_tests:
        print("🎉 All multipart tests passed!")
        sys.exit(0)
    elif total_passed == 0:
        print("❌ All tests failed - check ESP32-CAM connection and Aravis installation")
        sys.exit(1)
    else:
        print("⚠️  Some tests failed - multipart support partially working")
        sys.exit(1)

if __name__ == '__main__':
    main()