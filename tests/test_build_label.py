# ProsperoStore - A pull request's build names itself.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later
import os
import re
import subprocess
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


class BuildLabelTests(unittest.TestCase):
    def test_pull_request_artifact_and_label(self):
        workflow = (ROOT / ".github/workflows/tooling.yml").read_text(encoding="utf-8")
        self.assertIn(
            'echo "artifact=${GITHUB_REPOSITORY##*/}-PR$PR_NUMBER-$short" >> "$GITHUB_OUTPUT"',
            workflow,
        )
        self.assertIn('echo "BUILD_LABEL=PR $PR_NUMBER, $short" >> "$GITHUB_ENV"', workflow)
        self.assertIn("PR_HEAD: ${{ github.event.pull_request.head.sha }}", workflow)
        self.assertIn("name: ${{ steps.label.outputs.artifact }}", workflow)
        # The release job still finds a tag's build under its commit.
        self.assertIn('echo "artifact=prosperostore-$GITHUB_SHA" >> "$GITHUB_OUTPUT"', workflow)
        self.assertIn('--name "prosperostore-$GITHUB_SHA"', workflow)
        # A contributor's code is never built with write access or secrets.
        self.assertNotIn("pull_request_target:", workflow)

    def test_release_zip_is_attested_before_upload(self):
        workflow = (ROOT / ".github/workflows/tooling.yml").read_text(encoding="utf-8")
        # The finished ZIP, and only the ZIP, is attested before it is uploaded: pinned action,
        # never for a pull request or in a private repository.
        attest = workflow.index("- name: Attest the release ZIP")
        upload = workflow.index("- name: Upload build")
        self.assertLess(workflow.index("- name: Check the ZIP and write its checksum"), attest)
        self.assertLess(attest, upload)
        step = workflow[attest:upload]
        self.assertIn(
            "uses: actions/attest@1e69f48acb82d1966a394da916b4c1698aa569d6 # v4.2.2", step
        )
        self.assertIn(
            "if: github.event_name != 'pull_request' && !github.event.repository.private", step
        )
        self.assertIn("subject-path: dist/${{ env.TITLE_ID }}.zip\n", step)
        self.assertNotIn("SHA256SUMS", step)
        build_job = workflow[workflow.index("\n  build:") : workflow.index("\n  release:")]
        for permission in ("contents: read", "id-token: write", "attestations: write"):
            self.assertIn(f"      {permission}\n", build_job)
        self.assertNotIn("id-token", workflow.replace(build_job, ""))

    def test_build_checks_the_label_first_and_writes_it(self):
        build = (ROOT / "tools/build.sh").read_text(encoding="utf-8")
        self.assertIn("printf '%s\\n' \"$BUILD_LABEL\" > \"$app/build-label.txt\"", build)
        self.assertIn("{1,40}$", build)
        self.assertLess(build.index("BUILD_LABEL must be"), build.index("setup-native-dependencies"))
        self.assertLess(build.index("BUILD_LABEL must be"), build.index("ninja_run\n\napp="))
        # The label never reaches the version.
        self.assertEqual(len(re.findall("BUILD_LABEL", build)), 5)

    def test_build_refuses_a_bad_label_before_building(self):
        for label in ("PR 12; rm -rf", "a" * 41, "new\nline", "quote'", "$(id)", "é"):
            result = subprocess.run(
                ["bash", str(ROOT / "tools/build.sh")],
                env={**os.environ, "BUILD_LABEL": label},
                capture_output=True,
                text=True,
                check=False,
            )
            self.assertEqual(result.returncode, 2, label)
            self.assertIn("BUILD_LABEL must be", result.stderr)
            self.assertEqual(result.stdout, "")


if __name__ == "__main__":
    unittest.main()
