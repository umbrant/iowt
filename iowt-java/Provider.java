import java.io.*;
import java.net.*;
public class Provider{
	ServerSocket providerSocket;
	Socket connection = null;
	DataOutputStream out;
	DataInputStream in;

	byte[] buffer = new byte[100];
	byte[] filebytes;

	Provider()
	{
		try {
			providerSocket = new ServerSocket(8002, 10);
		} catch(IOException e) {
			e.printStackTrace();
			System.exit(-1);
		}
	}
	void close()
	{
		try {
			providerSocket.close();
		} catch(IOException e) {
			e.printStackTrace();
			System.exit(-1);
		}
	}
	void run()
	{
		try{
			System.out.println("Waiting for connection");
			connection = providerSocket.accept();
			System.out.println("Connection received from " + connection.getInetAddress().getHostName());
			out = new DataOutputStream(connection.getOutputStream());
			in = new DataInputStream(connection.getInputStream());
			//out.flush();

			// Read in the message from the client
			int available = in.available();
			if(available > 100) {
				available = 100;
			}
			int bytes_read = in.read(buffer, 0, available);
			System.out.println("client>" + buffer);

			// Write a reply
			sendMessage(filebytes);
		}
		catch(IOException ioException){
			ioException.printStackTrace();
		}
		finally{
			//4: Closing connection
			try{
				out.flush();
				Thread.sleep(100);
				in.close();
				out.close();
				connection.close();
			}
			catch(IOException ioException){
				ioException.printStackTrace();
			}
			catch(InterruptedException e) {
				e.printStackTrace();
			}
		}
	}
	void sendMessage(byte[] msg)
	{
		try{
			out.write(msg, 0, msg.length);
			out.flush();
			System.out.println("Sent " + msg.length + " bytes");
		}
		catch(IOException ioException){
			ioException.printStackTrace();
		}
	}
	void getBytesFromFile(File file) throws IOException {
        InputStream is = new FileInputStream(file);

        // Get the size of the file
        long length = file.length();
        // Create the byte array to hold the data
        filebytes = new byte[(int)length];
        // Read in the bytes
        int offset = 0;
        int numRead = 0;
        while (offset < filebytes.length
               	&& (numRead=is.read(filebytes, offset, filebytes.length-offset)) >= 0)
        {
            offset += numRead;
        }
        // Ensure all the bytes have been read in
        if (offset < filebytes.length) {
            throw new IOException("Could not completely read file "+file.getName());
        }

        // Close the input stream and return bytes
        is.close();
    }
	public static void main(String args[])
	{
		File f = new File(args[0]);
		while(true) {
			Provider server = new Provider();
			try {
				server.getBytesFromFile(f);
			} catch(IOException e) {
				e.printStackTrace();
			}
			server.run();
			server.close();
		}
	}
}
