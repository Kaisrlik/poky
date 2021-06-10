# SPDX-License-Identifier: GPL-2.0-only

"""Helper module for sha256sum checks"""
import bb
import os
import subprocess

class ManifestCheck(object):
    """Class for handling local (on the build host) checking"""
    def __init__(self, d):
        self.sha256sum_bin = d.getVar('SHA256SUM_BIN') or \
                bb.utils.which(os.getenv('PATH'), 'sha256sum')
        self.sha256sum_cmd = [self.sha256sum_bin]
        self.sha256sum_version = self.get_sha256sum_version()


    def generate_manifest(self, input_file):
        """Create a manifest for given file"""
        cmd = [self.sha256sum_bin, input_file]
        manifest_file = input_file + '.manifest'

        try:
            with open(manifest_file, "w") as manifest:
                job = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
                if job.returncode:
                    bb.fatal("sha256sum exited with code %d: %s" % (job.returncode, stderr.decode("utf-8")))
                output = job.stdout.read().decode("utf-8")

                input_file_dir = os.path.dirname(input_file)
                manifest.write(output.replace(input_file_dir + "/", ""))

        except IOError as e:
            bb.error("IO error (%s): %s" % (e.errno, e.strerror))
            raise Exception("Failed to generate manifest file '%s'" % manifest_file)

        except OSError as e:
            bb.error("OS error (%s): %s" % (e.errno, e.strerror))
            raise Exception("Failed to generate manifest file '%s'" % manifest_file)


    def get_sha256sum_version(self):
        """Return the sha256version version as a tuple of ints"""
        try:
            cmd = self.sha256sum_cmd + ["--version"]
            ver_str = subprocess.check_output(cmd).split()[3].decode("utf-8")
            return tuple([int(i) for i in ver_str.split("-")[0].split('.')])
        except subprocess.CalledProcessError as e:
            bb.fatal("Could not get sha256sum version: %s" % e)


    def verify(self, sig_file):
        """Verify content"""
        cmd = self.sha256sum_cmd + [" --check"]
        cmd += [sig_file]
        job = subprocess.Popen(cmd, stderr=subprocess.PIPE)
        if job.returncode:
            bb.fatal("sha256sum check exited with code %d: %s" % (job.returncode, job.stderr.decode("utf-8")))
        ret = False if job.returncode else True
        return ret


def get_class(d):
    """Get object"""
    return ManifestCheck(d)
