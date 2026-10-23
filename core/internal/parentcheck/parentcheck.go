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
		// F-Dpi: the Windows GUI is built as F-Dpi.exe (CMakeLists.txt OUTPUT_NAME); other platforms keep "Throne".
		if !strings.EqualFold(parentDir, selfDir) || !strings.EqualFold(parentBase, "F-Dpi.exe") {
			log.Fatalf("parent check failed: unexpected parent %q, selfPath is %q", parentPath, selfPath)
		}
		return
	}

	if parentDir != selfDir || parentBase != "Throne" {
		log.Fatalf("parent check failed: unexpected parent %q, selfPath is %q", parentPath, selfPath)
	}
}
