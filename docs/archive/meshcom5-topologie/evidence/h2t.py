import re,html,sys
s=open(sys.argv[1]).read()
s=re.sub(r'<style.*?</style>','',s,flags=re.S); s=re.sub(r'<script.*?</script>','',s,flags=re.S)
s=re.sub(r'<svg.*?</svg>','[SVG]',s,flags=re.S)
s=re.sub(r'<(tr)[^>]*>','\n',s); s=re.sub(r'<(td|th)[^>]*>',' | ',s)
s=re.sub(r'<(p|li|h[1-6]|div|pre|br)[^>]*>','\n',s); s=re.sub(r'<[^>]+>','',s)
s=html.unescape(s); s=re.sub(r'\n\s*\n+','\n',s)
print(s)
