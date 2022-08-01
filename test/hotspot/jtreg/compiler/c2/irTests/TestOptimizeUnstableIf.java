/*
 * Copyright Amazon.com Inc. or its affiliates. All Rights Reserved.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This code is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 only, as
 * published by the Free Software Foundation.
 *
 * This code is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * version 2 for more details (a copy is included in the LICENSE file that
 * accompanied this code).
 *
 * You should have received a copy of the GNU General Public License version
 * 2 along with this work; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Please contact Oracle, 500 Oracle Parkway, Redwood Shores, CA 94065 USA
 * or visit www.oracle.com if you need additional information or have any
 * questions.
 */

package compiler.c2.irTests;

import jdk.test.lib.Asserts;
import compiler.lib.ir_framework.*;

/*
 * @test
 * @bug 8286104
 * @summary Test that C2 uses aggressive liveness to get rid of the boxing object which is
 *  only consumed by uncommon_trap.
 * @library /test/lib /
 * @run driver compiler.c2.irTests.TestOptimizeUnstableIf
 */
public class TestOptimizeUnstableIf {

    public static void main(String[] args) {
        TestFramework.run();
    }

    @Test
    @Arguments({Argument.MAX}) // the argument needs to be big enough to fall out of cache.
    @IR(failOn = {IRNode.ALLOC_OF, "Integer"})
    public static int boxing_object(int value) {
        Integer ii = Integer.valueOf(value);
        int sum = 0;

        if (value > 999_999) {
            sum += ii.intValue();
        }

        return sum;
    }

    // a custom "boxing" class
    public static class Value {
        public int _value;

        public Value(int value) {
          this._value = value;
        }
    }

    @Test
    @Arguments({Argument.TRUE, Argument.DEFAULT, Argument.RANDOM_EACH, Argument.RANDOM_EACH})
    @IR(failOn = {IRNode.UNSTABLE_IF_TRAP})
    public static int superficial_if(boolean cond, int i, int j, int k) {
        Value x = new Value(i);
        Value y = new Value(j);
        Value z = new Value(k);

        if (cond) { // likely
            i++;
        } // else section is superficial

        return x._value + y._value + z._value + i;
    }

    @Test
    @Arguments({Argument.RANDOM_EACH, Argument.TRUE})
    @IR(failOn = {IRNode.MUL})
    @IR(counts = {IRNode.UNSTABLE_IF_TRAP, "1"})
    public static int superfical_if_constant(int x, boolean cond) {
        int i = x;
        if (cond) { // likely
            i = 0;
        }
        // really complex iterations I made up
        int y  = 42;
        y = (int)(Math.sqrt(y) + (x * 0.1191837));
        // if constant folding works, y will be dead and return 0;
        return  i * y;
    }

    @Check(test = "boxing_object")
    public void checkWithTestInfo(int result, TestInfo info) {
        if (info.isWarmUp()) {
            // Accessing the cached boxing object during warm-up phase. It prevents parser from pruning that branch of Interger.valueOf();
            // This guarantees that a phi node is generated, which merge a cached object and the newly allocated object. eg.
            // 112:  Phi  ===  108  168  188  [[ 50 ]]  #java/lang/Integer:NotNull:exact *  Oop:java/lang/Integer:NotNull:exact *
            // 168: a cached object
            // 188: result of AllocateNode
            //  50: uncommon_trap unstable_if
            value += Integer.valueOf(0);
        }

        Asserts.assertEQ(result, Integer.MAX_VALUE);
    }

    public static Integer value = Integer.valueOf(0);
}
