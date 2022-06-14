/*
 * Copyright (c) 2022, Oracle and/or its affiliates. All rights reserved.
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
 *
 */

/**
 * @test
 * @bug 8280902 8281000 8281003 8281006 8281001
 * @summary Test uses custom launcher that starts VM using JNI that verifies
 *          various API called with a null caller class function properly.
 * @library /test/lib
 * @modules java.base/jdk.internal.module
 *          jdk.compiler
 * @build NullCallerTest
 *        jdk.test.lib.compiler.CompilerUtils
 * @requires os.family != "aix"
 * @run main/native NullCallerTest
 */

// Test disabled on AIX since we cannot invoke the JVM on the primordial thread.

import java.io.File;
import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.util.List;
import java.util.Properties;

import jdk.test.lib.Platform;
import jdk.test.lib.process.OutputAnalyzer;
import jdk.test.lib.compiler.CompilerUtils;

public class NullCallerTest {

    private static final String TEST_SRC = System.getProperty("test.src");

    // the module name of the test module
    private static final String TEST_MODULE = "n";

    private static final Path SRC_DIR    = Paths.get(TEST_SRC, "src");
    private static final Path MODS_DIR   = Paths.get("mods");
    private static final Path TEST_MOD_DIR = MODS_DIR.resolve(TEST_MODULE);
    private static final Path TEST_MOD_SRC_DIR = SRC_DIR.resolve(TEST_MODULE);

    /*
     * Build the test module called 'n' which opens the package 'open'
     * to everyone.  There is also a package 'closed' which is neither
     * opened or exported.
     */
    static void compileTestModule() throws Exception {
        // javac -d mods/$TESTMODULE src/$TESTMODULE/**
        boolean compiled = CompilerUtils.compile(SRC_DIR.resolve(TEST_MODULE), TEST_MOD_DIR);
        assert (compiled);

        // copy resources
        var resources = List.of("open/test.txt", "closed/test.txt", "open/NullCallerResource.properties");
        resources.stream().forEach(r -> {
            try {
                Files.copy(TEST_MOD_SRC_DIR.resolve(r), TEST_MOD_DIR.resolve(r));
            } catch (IOException e) {
                throw new RuntimeException("Failed to copy resource: " + r, e);
            }
        });
    }

    public static void main(String[] args) throws Exception {

        // build the module used for the test
        compileTestModule();

        var launcher = Path.of(System.getProperty("test.nativepath"), "NullCallerTest");
        var pb = new ProcessBuilder(launcher.toString());
        var env = pb.environment();

        var libDir = Platform.libDir().toString();
        var vmDir = Platform.jvmLibDir().toString();

        // set up shared library path
        var sharedLibraryPathEnvName = Platform.sharedLibraryPathVariableName();
        env.compute(sharedLibraryPathEnvName,
                (k, v) -> (v == null) ? libDir : v + File.pathSeparator + libDir);
        env.compute(sharedLibraryPathEnvName,
                (k, v) -> (v == null) ? vmDir : v + File.pathSeparator + vmDir);

        // launch the actual test
        System.out.println("Launching: " + launcher + " shared library path: " +
                env.get(sharedLibraryPathEnvName));
        new OutputAnalyzer(pb.start())
                .outputTo(System.out)
                .errorTo(System.err)
                .shouldHaveExitValue(0);

    }

}

