import React from 'react'
import { createRoot } from 'react-dom/client'
import App from './App'
import ProfileStudio from './ProfileStudio'
import './styles.css'
import './AirCarePanel.css'
createRoot(document.getElementById('root')!).render(<React.StrictMode>{location.pathname === '/engineering' ? <App/> : <ProfileStudio/>}</React.StrictMode>)
