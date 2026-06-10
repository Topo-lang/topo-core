package app;

public class Calculator {
    public static long add(long a, long b) {
        return a + b;
    }

    // Deliberately NOT declared in main.topo — the completeness check must
    // report this symbol as an error, and a plain `topo-build` (no flags,
    // no optimization sections) must therefore exit non-zero.
    public static long subtract(long a, long b) {
        return a - b;
    }
}
