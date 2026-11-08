//go:build !debug && !noparentcheck

package parentcheck

import (
	"log"
	"os"
	"path/filepath"
	"runtime"
	"strings"
)

func CheckParentProcess() {
	parentPath, err := getParentExePath(ParentPID)
	if err != nil {
		log.Fatalf("parent check: cannot read parent executable: %v", err)
	}
	parentPath = resolveFinalPath(parentPath)

	selfPath, err := os.Executable()
	if err != nil {
		log.Fatalf("parent check: cannot read own executable: %v", err)
	}
	selfPath = resolveFinalPath(selfPath)

	selfDir := filepath.Dir(selfPath)
	parentDir := filepath.Dir(parentPath)
	parentBase := filepath.Base(parentPath)

	if runtime.GOOS == "windows" {
		// F-Dpi: the GUI is built as F-Dpi.exe / F-Dpi (CMakeLists.txt OUTPUT_NAME); macOS keeps "Throne".
		if !strings.EqualFold(parentDir, selfDir) || !strings.EqualFold(parentBase, "F-Dpi.exe") {
			log.Fatalf("parent check failed: unexpected parent %q, selfPath is %q", parentPath, selfPath)
		}
		return
	}

	// A NixOS security wrapper (/run/wrappers/bin) execs the real core, so selfPath is still the package's own copy.
	expected := "Throne"
	if runtime.GOOS == "linux" {
		expected = "F-Dpi"
	}
	if parentDir != selfDir || parentBase != expected {
		log.Fatalf("parent check failed: unexpected parent %q, selfPath is %q", parentPath, selfPath)
	}
}
