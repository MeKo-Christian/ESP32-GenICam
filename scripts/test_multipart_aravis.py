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

def discover_esp32_camera():
    """Discover ESP32-CAM device and return its name"""
    for attempt in range(3):
        success, stdout, stderr = run_command("arv-tool-0.10", timeout=10)
        
        if success and stdout:
            # Look for ESP32-related device names
            lines = stdout.strip().split('\n')
            for line in lines:
                if 'ESP32' in line or 'GenICam' in line:
                    # Extract device name from format "DeviceName (IP)"
                    if '(' in line:
                        device_name = line.split('(')[0].strip()
                        return device_name
            
        if attempt < 2:
            print(f"⚠️  Discovery attempt {attempt + 1} failed, retrying...")
            time.sleep(2)
    
    return None

def test_aravis_discovery():
    """Test basic Aravis discovery"""
    print("Testing Aravis discovery...")
    
    device_name = discover_esp32_camera()
    
    if device_name:
        print(f"✅ ESP32-CAM discovered by Aravis: {device_name}")
        return True, device_name
    else:
        print("❌ ESP32-CAM not discovered by Aravis")
        print("Tip: Ensure ESP32-CAM is running and on the same network")
        return False, None

def test_chunk_mode_active(device_name):
    """Test ChunkModeActive feature access"""
    print("\nTesting ChunkModeActive feature...")
    
    if not device_name:
        print("❌ No device name provided")
        return False
    
    # Try to read ChunkModeActive feature
    cmd = f'arv-tool-0.10 -n "{device_name}" control ChunkModeActive'
    success, stdout, stderr = run_command(cmd, timeout=10)
    
    if success:
        value = stdout.strip() if stdout.strip() else "<empty>"
        print(f"✅ ChunkModeActive feature accessible: {value}")
        return True
    else:
        print("❌ ChunkModeActive feature not accessible")
        if stderr:
            print(f"Error: {stderr}")
        print(f"Command used: {cmd}")
        return False

def test_chunk_component_selector(device_name):
    """Test ChunkComponentSelector feature"""
    print("\nTesting ChunkComponentSelector feature...")
    
    if not device_name:
        print("❌ No device name provided")
        return False
    
    cmd = f'arv-tool-0.10 -n "{device_name}" control ChunkComponentSelector'
    success, stdout, stderr = run_command(cmd, timeout=10)
    
    if success:
        value = stdout.strip() if stdout.strip() else "<empty>"
        print(f"✅ ChunkComponentSelector feature accessible: {value}")
        return True
    else:
        print("❌ ChunkComponentSelector feature not accessible")
        if stderr:
            print(f"Error: {stderr}")
        print(f"Command used: {cmd}")
        return False

def test_sccfg_register(device_name):
    """Test direct SCCFG register access via features"""
    print("\nTesting direct SCCFG register (0x0d24) access...")
    
    if not device_name:
        print("❌ No device name provided")
        return False
    
    # Note: arv-tool doesn't have direct register access, so we'll test via features
    cmd = f'arv-tool-0.10 -n "{device_name}" features'
    success, stdout, stderr = run_command(cmd, timeout=10)
    
    if success and stdout:
        # Look for chunk-related features
        chunk_features = []
        for line in stdout.split('\n'):
            if 'chunk' in line.lower() or 'multipart' in line.lower():
                chunk_features.append(line.strip())
        
        if chunk_features:
            print("✅ Chunk/multipart features found:")
            for feature in chunk_features:
                print(f"  {feature}")
            return True
        else:
            print("❌ No chunk/multipart features found in feature list")
            print("Available features:")
            for line in stdout.split('\n')[:10]:  # Show first 10 features
                if line.strip():
                    print(f"  {line.strip()}")
            return False
    else:
        print("❌ Failed to retrieve features list")
        if stderr:
            print(f"Error: {stderr}")
        print(f"Command used: {cmd}")
        return False

def test_xml_multipart_features(device_name):
    """Test if multipart features are in GenICam XML"""
    print("\nTesting GenICam XML for multipart features...")
    
    if not device_name:
        print("❌ No device name provided")
        return False
    
    # Get the GenICam XML
    cmd = f'arv-tool-0.10 -n "{device_name}" genicam'
    success, stdout, stderr = run_command(cmd, timeout=10)
    
    if success and stdout:
        # Look for chunk/multipart related terms
        xml_content = stdout.lower()
        multipart_terms = ['chunk', 'multipart']
        found_features = []
        
        for term in multipart_terms:
            if term in xml_content:
                # Find lines containing the term
                for line in stdout.split('\n'):
                    if term in line.lower():
                        found_features.append(line.strip())
        
        if found_features:
            print("✅ Multipart features found in GenICam XML:")
            # Remove duplicates and show unique features
            unique_features = list(set(found_features))
            for feature in unique_features[:10]:  # Show first 10 unique matches
                if feature:
                    print(f"  {feature}")
            return True
        else:
            print("❌ No multipart features found in GenICam XML")
            print("XML snippet (first 5 lines):")
            for line in stdout.split('\n')[:5]:
                if line.strip():
                    print(f"  {line.strip()}")
            return False
    else:
        print("❌ Failed to retrieve GenICam XML")
        if stderr:
            print(f"Error: {stderr}")
        print(f"Command used: {cmd}")
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
    discovery_result, device_name = test_aravis_discovery()
    results.append(discovery_result)
    
    if args.quick:
        if results[0]:
            print("\n✅ Quick test passed - ESP32-CAM discovered")
            sys.exit(0)
        else:
            print("\n❌ Quick test failed - ESP32-CAM not discovered")
            sys.exit(1)
    
    # Skip remaining tests if discovery failed
    if not discovery_result:
        print("\n❌ Discovery failed - skipping remaining tests")
        print("Please ensure ESP32-CAM is running and accessible")
        # Add placeholder results for remaining tests
        results.extend([False, False, False, False])
    else:
        print(f"\nUsing discovered device: {device_name}")
        
        # Test 2: ChunkModeActive feature
        results.append(test_chunk_mode_active(device_name))
        
        # Test 3: ChunkComponentSelector feature  
        results.append(test_chunk_component_selector(device_name))
        
        # Test 4: SCCFG register access
        results.append(test_sccfg_register(device_name))
        
        # Test 5: XML multipart features
        results.append(test_xml_multipart_features(device_name))
    
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