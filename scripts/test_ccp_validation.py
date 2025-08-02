#!/usr/bin/env python3
"""
Quick test to verify the Control Channel Privilege validation logic.
Tests the same logic as implemented in the ESP32 code.
"""

def is_valid_privilege_value(value):
    """Python version of the C validation function for testing."""
    # According to GigE Vision specification, CCP register uses bitfields:
    # 0x00000000 - No access
    # 0x00000001 - Exclusive control (bit 0)
    # 0x00000200 - Primary control (bit 9) - used by Aravis and other tools
    # 0x00000201 - Both exclusive and primary (some clients)
    
    if (value == 0x00000000 or   # No access
        value == 0x00000001 or   # Exclusive control
        value == 0x00000200 or   # Primary control (Aravis standard)
        value == 0x00000201):    # Both bits
        return True
    
    return False

def test_validation():
    """Test the validation function with various values."""
    test_cases = [
        # (value, expected_result, description)
        (0x00000000, True, "No access"),
        (0x00000001, True, "Exclusive control (bit 0)"),
        (0x00000200, True, "Primary control (bit 9) - Aravis"),
        (0x00000201, True, "Both exclusive and primary"),
        (0x00000002, False, "Invalid - bit 1"),
        (0x00000100, False, "Invalid - bit 8"),
        (0x00000400, False, "Invalid - bit 10"),
        (0x00001000, False, "Invalid - bit 12"),
        (512, True, "Aravis value (decimal 512 = 0x200)"),
        (513, True, "Both bits (decimal 513 = 0x201)"),
        (1, True, "Exclusive control (decimal 1)"),
        (0, True, "No access (decimal 0)"),
        (2, False, "Invalid decimal 2"),
        (5, False, "Invalid decimal 5"),
    ]
    
    print("🧪 Testing Control Channel Privilege Validation Logic")
    print("=" * 60)
    
    passed = 0
    failed = 0
    
    for value, expected, description in test_cases:
        result = is_valid_privilege_value(value)
        status = "✅ PASS" if result == expected else "❌ FAIL"
        
        if result == expected:
            passed += 1
        else:
            failed += 1
        
        print(f"{status} | 0x{value:08x} ({value:>4}) | {description}")
        if result != expected:
            print(f"      Expected: {expected}, Got: {result}")
    
    print("=" * 60)
    print(f"📊 Results: {passed} passed, {failed} failed")
    
    if failed == 0:
        print("🎉 All tests passed! The validation logic is correct.")
        print("\n🔍 Key findings:")
        print("  ✅ Aravis value 0x200 (512) is now accepted")
        print("  ✅ Standard GigE Vision bitfield values work")
        print("  ✅ Invalid values are properly rejected")
    else:
        print("⚠️  Some tests failed. Review the validation logic.")
    
    return failed == 0

if __name__ == "__main__":
    test_validation()