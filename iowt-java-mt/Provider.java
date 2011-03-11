import java.io.*;
import java.net.*;
public class Provider{
	ServerSocket providerSocket;
	Socket connection = null;

	byte[] filebytes;

	Provider(String filename)
	{
		try {
			getBytesFromFile(new File(filename));
		} catch(IOException e) {
			e.printStackTrace();
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
		try {
			providerSocket = new ServerSocket(8002, 10);
		} catch(IOException e) {
			e.printStackTrace();
			System.exit(-1);
		}
		while(true) {
			try {
				System.out.println("Waiting for connection");
				connection = providerSocket.accept();
				System.out.println("Connection received from " + connection.getInetAddress().getHostName());

				new Thread(
            			new WorkerRunnable(connection, filebytes)
        				).start();
			}
			catch(IOException ioException){
				ioException.printStackTrace();
			}
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
		Provider server = new Provider(args[0]);
		server.run();
	}
}

class WorkerRunnable implements Runnable {

    protected Socket clientSocket = null;
    byte[] filebytes = null;
    protected String serverText   = null;

    public WorkerRunnable(Socket clientSocket, byte[] filebytes) {
        this.clientSocket = clientSocket;
        this.filebytes = filebytes;
    }

    public void run() {
        try {
            InputStream input  = clientSocket.getInputStream();
            OutputStream output = clientSocket.getOutputStream();
			// Read in the message from the client
			byte[] buffer = new byte[100];
			int available = input.available();
			if(available > 100) {
				available = 100;
			}
			int bytes_read = input.read(buffer, 0, available);
			System.out.println("client>" + buffer);
			// Write a reply
            output.write(filebytes, 0, filebytes.length);
            output.flush();
            output.close();
            input.close();
            System.out.println("Sent " + filebytes.length + " bytes");
        } catch (IOException e) {
            //report exception somewhere.
            e.printStackTrace();
        }
    }
}

