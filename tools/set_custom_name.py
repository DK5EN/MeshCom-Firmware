Import("env")

env.Replace(PROGNAME="%s" % env.GetProjectOption("custom_filename"))