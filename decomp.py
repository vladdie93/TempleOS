import glob
import subprocess as sp


glob_list = glob.glob('./**/.Z', recursive=True)

for entry in glob_list:
  sp.call(['TOSZ', entry, entry.rstrip('.Z')])



