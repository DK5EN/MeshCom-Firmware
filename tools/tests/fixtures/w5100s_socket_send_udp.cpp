	SPI.beginTransaction(SPI_ETHERNET_SETTINGS);
	W5100.writeSnDIPR(s, addr);
	W5100.writeSnDPORT(s, port);
	SPI.endTransaction();
	return true;
}

bool EthernetClass::socketSendUDP(uint8_t s)
{
	SPI.beginTransaction(SPI_ETHERNET_SETTINGS);
	W5100.execCmdSn(s, Sock_SEND);

	/* +2008.01 bj */
	while ( (W5100.readSnIR(s) & SnIR::SEND_OK) != SnIR::SEND_OK ) {
		if (W5100.readSnIR(s) & SnIR::TIMEOUT) {
			/* +2008.01 [bj]: clear interrupt */
			W5100.writeSnIR(s, (SnIR::SEND_OK|SnIR::TIMEOUT));
			SPI.endTransaction();
			//Serial.printf("sendUDP timeout\n");
			return false;
		}
		SPI.endTransaction();
		yield();
		SPI.beginTransaction(SPI_ETHERNET_SETTINGS);
	}

	/* +2008.01 bj */
	W5100.writeSnIR(s, SnIR::SEND_OK);
	SPI.endTransaction();

	//Serial.printf("sendUDP ok\n");
	/* Sent ok */
	return true;
}
