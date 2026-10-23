// TeXworksScript
// Title: Open in default viewer
// Description: Attempts to open the PDF file using the system's default viewer
// Author: Stefan Löffler & Jonathan Kew
// Version: 0.2.1
// Date: 2011-09-12
// Script-Type: standalone
// Context: PDFDocument


var retVal = TW.launchFile(TW.target.fileName);

if (retVal["status"] !== 0)
{
	TW.information(TW.target, "Error", retVal["message"]);
}

undefined;
