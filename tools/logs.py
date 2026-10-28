import asyncio, sys, time, aioesphomeapi
KEY="API_SCHLUESSEL_BASE64"
async def main(dur):
    end=time.time()+dur
    while time.time()<end:
        cli=aioesphomeapi.APIClient(sys.argv[2] if len(sys.argv)>2 else "192.168.1.60",6053,None,noise_psk=KEY)
        try:
            await cli.connect(login=True)
            print("### CONNECTED", time.strftime("%H:%M:%S"), flush=True)
            cli.subscribe_logs(lambda l: print(time.strftime("%H:%M:%S"), l.message.decode(errors="replace"), flush=True), log_level=aioesphomeapi.LogLevel.LOG_LEVEL_DEBUG)
            while time.time()<end:
                await asyncio.sleep(1)
                if cli._connection is None or not cli._connection.is_connected: break
        except Exception as e:
            print("### ERR", repr(e)[:100], flush=True)
            await asyncio.sleep(2)
        try: await cli.disconnect()
        except Exception: pass
asyncio.run(main(int(sys.argv[1])))
