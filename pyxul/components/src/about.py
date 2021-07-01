from xpcom import Component, Components, GetFactory

import os
import platform
import sys


Ci = Components.interfaces
Cc = Components.classes


about = f"""<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE html PUBLIC "-//W3C//DTD XHTML 1.1//EN" "http://www.w3.org/TR/xhtml11/DTD/xhtml11.dtd">
<html xmlns="http://www.w3.org/1999/xhtml">
    <head>
        <title>about:python</title>
        <style>
        <![CDATA[
            body {{
                font-family: sans-serif;
            }}

            h1 {{
                text-align: center;
            }}
            table, td, th {{
                border: 1px solid #ddd;
                text-align: left;
                vertical-align: top;
            }}
            table {{
                border-collapse: collapse;
                table-layout: auto;
                width: 100%;
            }}
            th, td {{
                padding: 10px;
            }}
        ]]>
        </style>
    </head>
    <body>
        <h1>Python</h1>
        <table>
            <tr>
                <td><b>version</b></td>
                <td>{sys.version}</td>
            </tr>
            <tr>
                <td><b>platform</b></td>
                <td>{platform.platform()}</td>
            </tr>
            <tr>
                <td><b>paths</b></td>
                <td>{"<br />".join(sys.path)}</td>
            </tr>
            <tr>
                <td><b>modules</b></td>
                <td>{", ".join(sorted(sys.modules.keys()))}</td>
            </tr>
            <tr>
                <td><b>environment</b></td>
                <td>{"<br />".join((f'{k}={v}' for k, v in sorted(os.environ.items())))}</td>
            </tr>
        </table>
    </body>
</html>
"""


class AboutPython(Component):

    classID = Components.ID("{e7ed1572-da2b-11eb-83e5-db683632b179}")
    contractID = "@mozilla.org/network/protocol/about;1?what=python"
    classDescription = "about:python handler"
    interfaces = [Ci.nsIAboutModule]

    def getURIFlags(self, aURI):
        return (
            Ci.nsIAboutModule.URI_SAFE_FOR_UNTRUSTED_CONTENT |
            Ci.nsIAboutModule.MAKE_LINKABLE
        )

    def newChannel(self, aURI, aLoadInfo):
        aStream = getattr(
            Cc, "@mozilla.org/io/string-input-stream;1"
        ).createInstance(Ci.nsIStringInputStream)
        aStream.setData(about, len(about))
        aChannel = getattr(
            Cc, "@mozilla.org/network/input-stream-channel;1"
        ).createInstance(Ci.nsIInputStreamChannel)
        aChannel.setURI(aURI)
        aChannel.contentStream = aStream
        return aChannel


NSGetFactory = GetFactory(AboutPython)

